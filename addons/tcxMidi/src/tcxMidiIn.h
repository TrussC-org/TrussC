#pragma once

// =============================================================================
// tcxMidiIn.h - MIDI input port (libremidi backend)
// =============================================================================
// Two ways to consume incoming messages:
//
//   1. Event API (oF/TrussC style):
//        midiIn.onMessage.listen([](MidiMessage& m){ ... });
//      [!] The callback fires on libremidi's input thread, NOT the main
//          thread. Guard any shared state with a mutex.
//
//   2. Polling API (thread-safe, drain from update()):
//        MidiMessage m;
//        while (midiIn.getNextMessage(m)) { ... }
//      The internal queue is enabled on the first hasNewMessage()/
//      getNextMessage() call. It holds up to getBufferSize() messages
//      (default 1024), plus 64 MiB of retained objects, capacities, metadata
//      and queue nodes. Overflow evicts oldest whole messages; one over-budget
//      message is rejected. getDroppedMessageCount() counts these and shrink
//      losses. The first loss warns automatically on the main thread, once
//      per input lifetime across close/reopen, without another poll.
//
// Native SysEx assembly uses upstream libremidi without an addon size cap.
// WebMIDI access does not enable SysEx.
//
// Device lifecycle (docs/architecture/DEVICE_LIFECYCLE.md): isConnected(), setup(), close(),
// onDisconnect. When the OS reports the opened port removed (CoreMIDI, ALSA
// sequencer, WinMM; not WebMIDI), the port is closed, one warning is logged and
// onDisconnect fires once on the main thread, so a listener may call setup().
// close(), opening another port and the destructor never fire it.
//
//   if (!midiIn.isConnected() && getElapsedTime() - lastTry > 2.0f) {
//       lastTry = getElapsedTime();
//       midiIn.setup();                 // same port, found again by name
//   }
// =============================================================================

#include "tcxMidiMessage.h"

#include "tc/events/tcEvent.h"
#include "tc/events/tcEventArgs.h"
#include "tc/network/tcNetworkState.h"
#include "tc/utils/tcMainThread.h"
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcLog.h"
#include "tc/utils/tcPollingQueue.h"

#include <libremidi/libremidi.hpp>
#include "tcxMidiApi.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tcx::midi {

using namespace tc;

class MidiIn {
public:
    // Fired for every received message (on the backend thread - see header note).
    trussc::Event<MidiMessage> onMessage;

    // Fired once when the OS reports the opened port removed (the port is
    // already closed, isConnected() is false). Never from close(), a new open
    // or the destructor. Always on the main thread, in the next host turn after
    // the backend noticed. Not reported by WebMIDI or for virtual ports.
    trussc::Event<DeviceDisconnectEventArgs> onDisconnect;

    MidiIn() {
        messageTarget_ = tc::internal::EventSnapshotDelivery<MidiMessage>::bind(onMessage, delivery_);
    }
    ~MidiIn() { delivery_->close(); close(); }

    // Non-copyable (owns a backend handle).
    MidiIn(const MidiIn&) = delete;
    MidiIn& operator=(const MidiIn&) = delete;

    // -------------------------------------------------------------------------
    // Enumeration
    // -------------------------------------------------------------------------

    // Available input ports, in port-number order (see MidiDeviceInfo).
    // Open by name for stable selection; indices shift when ports appear or disappear.
    static std::vector<MidiDeviceInfo> listDevices() {
        std::vector<MidiDeviceInfo> devices;
        libremidi::observer obs{internal::midiObserverConfiguration(),
                               libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_input_ports();
        for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
            devices.push_back({i, ports[i].display_name});
        }
        return devices;
    }

    // -------------------------------------------------------------------------
    // Open / close
    // -------------------------------------------------------------------------

    // Open by port index.
    // Open by name for stable selection; indices shift when ports appear or disappear.
    bool openPort(int index) {
        libremidi::observer obs{internal::midiObserverConfiguration(),
                               libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_input_ports();
        if (index < 0 || index >= static_cast<int>(ports.size())) {
            trussc::logError("tcxMidiIn") << "openPort: index " << index
                                          << " out of range (" << ports.size() << " ports)";
            return false;
        }
        return openInputPort(ports[index], index);
    }

    // Open the first port whose name contains `nameContains` (case-sensitive).
    // Open by name for stable selection; indices shift when ports appear or disappear.
    bool openPort(const std::string& nameContains) {
        libremidi::observer obs{internal::midiObserverConfiguration(),
                               libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_input_ports();
        for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
            if (ports[i].display_name.find(nameContains) != std::string::npos) {
                return openInputPort(ports[i], i);
            }
        }
        trussc::logError("tcxMidiIn") << "openPort: no input matching \""
                                      << nameContains << "\"";
        return false;
    }

    // Create a virtual input port (CoreMIDI / ALSA / JACK; not on Windows).
    bool openVirtualPort(const std::string& name = "TrussC Input") {
        close();
        prepareInput(name, -1);
        auto err = backend_->input->open_virtual_port(name);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiIn") << "openVirtualPort failed: " << name;
            close();
            return false;
        }
        name_ = name;
        portNumber_ = -1;
        virtual_ = true;
        lastName_ = name;
        lastVirtual_ = true;
        return true;
    }

    // Reopen the last port that opened successfully, found again by its name
    // (indices shift when ports appear or disappear): the same virtual port, or
    // the input port whose name equals the earlier one exactly. Returns false
    // and logs an error when there is no such port yet or it cannot be opened.
    // This is the reconnect call:
    //   if (!midiIn.isConnected() && timerElapsed) midiIn.setup();
    bool setup() {
        if (lastName_.empty()) {
            trussc::logError("tcxMidiIn") << "setup() has no previous port to reopen; call openPort() first";
            return false;
        }
        if (lastVirtual_) return openVirtualPort(lastName_);
        libremidi::observer obs{internal::midiObserverConfiguration(),
                               libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_input_ports();
        for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
            if (ports[i].display_name == lastName_) return openInputPort(ports[i], i);
        }
        trussc::logError("tcxMidiIn") << "setup: no input named \"" << lastName_ << "\"";
        return false;
    }

    // Close the port. Never fires onDisconnect. From an input callback, backend
    // teardown is queued on the main thread. The callback only marks this run
    // closed and never waits for that queue.
    void close() {
#if !defined(__EMSCRIPTEN__)
        watch_.reset();
#endif
        auto state = std::move(state_);
        auto backend = std::move(backend_);
        if (state) state->open = false;
        name_.clear();
        portNumber_ = -1;
        virtual_ = false;
        if (!backend) return;
        if (state && callbackState_ == state.get()) {
            runOnMainThread([backend] {
                backend->input->close_port();
                backend->input.reset();
            }, backend->closeToken);
        } else {
            backend->input->close_port();
            backend->input.reset();
        }
    }

    // True only while the port is open; false once the OS reported it removed.
    bool isConnected() const { return state_ && state_->open.load(); }
    // Older names of isConnected() / close().
    bool isOpen() const { return isConnected(); }
    void closePort() { close(); }
    const std::string& getName() const { return name_; }   // name of the open port
    int  getPort() const { return portNumber_; }           // open port number, -1 if virtual/closed
    bool isVirtual() const { return virtual_; }

    // -------------------------------------------------------------------------
    // Filtering (call before openPort)
    // -------------------------------------------------------------------------
    // By default sysex passes through (needed for controllers like Launchpad),
    // while timing clock and active sensing are ignored to avoid flooding.
    void ignoreTypes(bool sysex, bool timing, bool sensing) {
        ignoreSysex_ = sysex;
        ignoreTiming_ = timing;
        ignoreSensing_ = sensing;
    }

    // -------------------------------------------------------------------------
    // Polling API (thread-safe)
    // -------------------------------------------------------------------------

    // Enable polling on first call; closing/reopening preserves queued messages.
    bool hasNewMessage() { return polling_->hasNewMessage(); }
    bool getNextMessage(MidiMessage& msg) { return polling_->getNextMessage(msg); }
    // Queue limit (default 1024); overflow and shrinking drop the oldest first.
    // Device input or a stalled reader can fill this queue; count and 64 MiB retained-storage caps keep memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
    void setBufferSize(size_t size) { polling_->setBufferSize(size); }
    // Device input or a stalled reader can fill this queue; count and 64 MiB retained-storage caps keep memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
    size_t getBufferSize() const { return polling_->getBufferSize(); }
    // Running total since construction, including drops caused by shrinking.
    uint64_t getDroppedMessageCount() const { return polling_->getDroppedMessageCount(); }

private:
    friend struct MidiInTestAccess;
    struct PollingStorage {
        static constexpr const char* module = "tcxMidiIn";
        static constexpr const char* store = "MIDI polling queue";
        static MidiMessage copy(const MidiMessage& msg) {
            MidiMessage result(msg.bytes, msg.deltatime);
            result.portName = tc::internal::pollingStringCopy(msg.portName);
            result.portNum = msg.portNum;
            return result;
        }
        static size_t copyStorage(const MidiMessage& msg) {
            return tc::internal::storageAdd(msg.bytes.size(), tc::internal::copiedStringStorage(msg.portName));
        }
        static size_t retainedStorage(const MidiMessage& msg) {
            return tc::internal::storageAdd(msg.bytes.capacity(), tc::internal::retainedStringStorage(msg.portName));
        }
    };
    using Polling = tc::internal::PollingQueue<MidiMessage, PollingStorage>;
    struct State {
        std::shared_ptr<Polling> polling;
        std::string name_;
        int portNumber_ = -1;
        std::atomic<bool> open{true};
        tc::internal::EventSnapshotDelivery<MidiMessage> onMessage;
        void handleMessage(const libremidi::message& m) {
            if (!open.load()) return;
            struct CallbackScope {
                const State* previous = callbackState_;
                explicit CallbackScope(const State* state) { callbackState_ = state; }
                ~CallbackScope() { callbackState_ = previous; }
            } scope(this);
            MidiMessage msg;
            msg.bytes.assign(m.bytes.begin(), m.bytes.end());
            msg.deltatime = static_cast<double>(m.timestamp) * 1e-9;  // ns -> s
            msg.portName = name_;
            msg.portNum = portNumber_;

            polling->enqueue(msg);
            if (open.load()) onMessage.notifyExternal(msg);
        }

    };
    inline static thread_local const State* callbackState_ = nullptr;

    // The backend owns its callback State until close_port()/unregistration
    // returns. State never owns the backend, avoiding a callback ownership cycle.
    struct Backend {
        std::unique_ptr<libremidi::midi_in> input;
        std::shared_ptr<std::atomic<bool>> closeToken = std::make_shared<std::atomic<bool>>(true);
    };

    libremidi::input_configuration makeConfig(const std::shared_ptr<State>& state) {
        libremidi::input_configuration cfg;
        cfg.on_message = [state](const libremidi::message& m) { state->handleMessage(m); };
        cfg.ignore_sysex = ignoreSysex_;
        cfg.ignore_timing = ignoreTiming_;
        cfg.ignore_sensing = ignoreSensing_;
        cfg.timestamps = libremidi::timestamp_mode::Relative;
        return cfg;
    }

    void prepareInput(const std::string& name, int port) {
        state_ = std::make_shared<State>();
        state_->polling = polling_;
        state_->name_ = name;
        state_->portNumber_ = port;
        state_->onMessage.target = messageTarget_;
        internal::keepMidiClientAlive();
        backend_ = std::make_shared<Backend>();
        backend_->input = std::make_unique<libremidi::midi_in>(
            makeConfig(state_), libremidi::midi_in_configuration_for(platformMidiApi()));
    }

    bool openInputPort(const libremidi::input_port& port, int index) {
        close();
        prepareInput(port.display_name, index);
        auto err = backend_->input->open_port(port);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiIn") << "open_port failed: " << port.display_name;
            close();
            return false;
        }
        name_ = port.display_name;
        portNumber_ = index;
        virtual_ = false;
        lastName_ = name_;
        lastVirtual_ = false;
#if !defined(__EMSCRIPTEN__)
        watch_ = std::make_unique<internal::PortWatch>(true, [this] { inputPortRemoved(); });
#endif
        return true;
    }

#if !defined(__EMSCRIPTEN__)
    // Main thread, after the OS removed some input port: if ours is no longer
    // listed, it was lost. The name is the identity, as for setup(); two ports
    // with the same name are not told apart.
    void inputPortRemoved() {
        if (!isConnected() || virtual_) return;
        libremidi::observer obs{internal::midiObserverConfiguration(),
                               libremidi::observer_configuration_for(platformMidiApi())};
        for (const auto& port : obs.get_input_ports())
            if (port.display_name == name_) return;
        const std::string lost = name_;
        close();   // isConnected() is false before the listeners run
        trussc::logWarning("tcxMidiIn") << "lost input port \"" << lost << "\"";
        DeviceDisconnectEventArgs args{"input port \"" + lost + "\" was removed"};
        onDisconnect.notify(args);
    }
#endif

    std::shared_ptr<Polling> polling_ = std::make_shared<Polling>();
    std::shared_ptr<State> state_;
    std::shared_ptr<Backend> backend_;
    std::shared_ptr<tc::internal::NetworkDelivery> delivery_ = std::make_shared<tc::internal::NetworkDelivery>();
    std::shared_ptr<tc::internal::EventSnapshotDelivery<MidiMessage>::Target> messageTarget_;
    std::string name_;
    int portNumber_ = -1;
    bool virtual_ = false;
    // The last port that opened successfully, for setup(); kept by close()
    std::string lastName_;
    bool lastVirtual_ = false;
#if !defined(__EMSCRIPTEN__)
    std::unique_ptr<internal::PortWatch> watch_;
#endif
    bool ignoreSysex_ = false;
    bool ignoreTiming_ = true;
    bool ignoreSensing_ = true;

};

}  // namespace tcx::midi

namespace tcx { using midi::MidiIn; } // deprecated: remove at v1.0.0
