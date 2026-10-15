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
//      (default 1024); when full, the oldest is dropped, counted in
//      getDroppedMessageCount() and logged as a warning from those two calls
//      (at most once every 2 s).
// =============================================================================

#include "tcxMidiMessage.h"

#include "tc/events/tcEvent.h"
#include "tc/network/tcNetworkState.h"
#include "tc/utils/tcMainThread.h"
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcLog.h"

#include <libremidi/libremidi.hpp>
#include "tcxMidiApi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace tcx::midi {

using namespace tc;

class MidiIn {
public:
    // Fired for every received message (on the backend thread - see header note).
    trussc::Event<MidiMessage> onMessage;

    MidiIn() {
        messageTarget_ = tc::internal::EventSnapshotDelivery<MidiMessage>::bind(onMessage, delivery_);
    }
    ~MidiIn() { delivery_->close(); closePort(); }

    // Non-copyable (owns a backend handle).
    MidiIn(const MidiIn&) = delete;
    MidiIn& operator=(const MidiIn&) = delete;

    // -------------------------------------------------------------------------
    // Enumeration
    // -------------------------------------------------------------------------

    // Available input ports, in port-number order (see MidiDeviceInfo).
    static std::vector<MidiDeviceInfo> listDevices() {
        std::vector<MidiDeviceInfo> devices;
        libremidi::observer obs{{}, libremidi::observer_configuration_for(platformMidiApi())};
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
    bool openPort(int index) {
        libremidi::observer obs{{}, libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_input_ports();
        if (index < 0 || index >= static_cast<int>(ports.size())) {
            trussc::logError("tcxMidiIn") << "openPort: index " << index
                                          << " out of range (" << ports.size() << " ports)";
            return false;
        }
        return openInputPort(ports[index], index);
    }

    // Open the first port whose name contains `nameContains` (case-sensitive).
    bool openPort(const std::string& nameContains) {
        libremidi::observer obs{{}, libremidi::observer_configuration_for(platformMidiApi())};
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
        closePort();
        prepareInput(name, -1);
        auto err = backend_->input->open_virtual_port(name);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiIn") << "openVirtualPort failed: " << name;
            closePort();
            return false;
        }
        name_ = name;
        portNumber_ = -1;
        virtual_ = true;
        return true;
    }

    // From an input callback, backend teardown is queued on the main thread.
    // The callback only marks this run closed and never waits for that queue.
    void closePort() {
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

    bool isOpen() const { return state_ && state_->open.load(); }
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
    bool hasNewMessage() { return polling_->hasNewMessage(name_); }
    bool getNextMessage(MidiMessage& msg) { return polling_->getNextMessage(msg, name_); }
    // Queue limit (default 1024); overflow and shrinking drop the oldest first.
    void setBufferSize(size_t size) { polling_->setBufferSize(size); }
    size_t getBufferSize() const { return polling_->getBufferSize(); }
    // Running total since construction, including drops caused by shrinking.
    uint64_t getDroppedMessageCount() const { return polling_->getDroppedMessageCount(); }

private:
    struct Polling {
        // Enables the internal queue on first call. Returns true if unread.
        bool hasNewMessage(const std::string& name) {
            bufferEnabled_ = true;
            reportDrops(name);
            std::lock_guard<std::mutex> lock(queueMutex_);
            return !queue_.empty();
        }

        // Pop the next queued message. Returns false when the queue is empty.
        bool getNextMessage(MidiMessage& msg, const std::string& name) {
            bufferEnabled_ = true;
            reportDrops(name);
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (queue_.empty()) return false;
            msg = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        // Queue limit (default 1024). Past it, the oldest message is dropped.
        // Shrinking the limit discards the oldest queued messages now; they
        // count as dropped too.
        void setBufferSize(size_t size) {
            uint64_t dropped = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                bufferMax_ = size;
                while (queue_.size() > bufferMax_) {
                    queue_.pop();
                    ++dropped;
                }
            }
            countDrops(dropped);
        }
        size_t getBufferSize() const { return bufferMax_; }

        // Messages received but never handed to the app, since this MidiIn was
        // created: dropped from the full polling queue, or discarded when
        // setBufferSize() shrank it (a running total, never reset). Cheap to
        // call from any thread.
        uint64_t getDroppedMessageCount() const {
            return droppedMessages_.load(std::memory_order_relaxed);
        }
        // Any thread: add drops to the total and to the next report. Only
        // touches atomics (no lock, no log), so the input thread can call it.
        void countDrops(uint64_t dropped) {
            if (dropped == 0) return;
            droppedMessages_.fetch_add(dropped, std::memory_order_relaxed);
            unreportedDrops_.fetch_add(dropped, std::memory_order_relaxed);
        }

        // Polling side: log the drops counted since the last report, at most
        // once every kDropReportInterval. Drops in between are summed into the
        // next line, so a queue that overflows every frame cannot flood the log.
        void reportDrops(const std::string& name) {
            if (unreportedDrops_.load(std::memory_order_relaxed) == 0) return;
            const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t last = lastDropReportNs_.load(std::memory_order_relaxed);
            if (last != kNeverReported &&
                now - last < std::chrono::nanoseconds(kDropReportInterval).count()) return;
            // Only one polling thread reports a given interval
            if (!lastDropReportNs_.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
            const uint64_t n = unreportedDrops_.exchange(0, std::memory_order_relaxed);
            if (n == 0) return;
            size_t limit;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                limit = bufferMax_;
            }
            trussc::logWarning("tcxMidiIn") << "MidiIn \"" << name << "\": " << n
                                            << (n == 1 ? " message" : " messages")
                                            << " dropped since the last report (queue limit "
                                            << limit << ", oldest dropped first); raise setBufferSize()";
        }

        std::queue<MidiMessage> queue_;
        std::mutex queueMutex_;
        std::atomic<bool> bufferEnabled_{false};
        size_t bufferMax_ = 1024;

        // Drop accounting: the input thread and setBufferSize() only add to the
        // counters (countDrops()); the polling calls read and log them.
        static constexpr std::chrono::seconds kDropReportInterval{2};
        static constexpr int64_t kNeverReported = INT64_MIN;
        std::atomic<uint64_t> droppedMessages_{0};   // running total
        std::atomic<uint64_t> unreportedDrops_{0};   // counted, not logged yet
        std::atomic<int64_t> lastDropReportNs_{kNeverReported};  // steady_clock
    };
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

            if (polling->bufferEnabled_) {
                // The input thread only counts drops; the polling calls log
                // them (reportDrops()).
                uint64_t dropped = 0;
                {
                    std::lock_guard<std::mutex> lock(polling->queueMutex_);
                    polling->queue_.push(msg);
                    while (polling->queue_.size() > polling->bufferMax_) {
                        polling->queue_.pop();
                        ++dropped;
                    }
                }
                polling->countDrops(dropped);
            }
            if (open.load()) onMessage.notify(msg);
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
        backend_ = std::make_shared<Backend>();
        backend_->input = std::make_unique<libremidi::midi_in>(
            makeConfig(state_), libremidi::midi_in_configuration_for(platformMidiApi()));
    }

    bool openInputPort(const libremidi::input_port& port, int index) {
        closePort();
        prepareInput(port.display_name, index);
        auto err = backend_->input->open_port(port);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiIn") << "open_port failed: " << port.display_name;
            closePort();
            return false;
        }
        name_ = port.display_name;
        portNumber_ = index;
        virtual_ = false;
        return true;
    }

    std::shared_ptr<Polling> polling_ = std::make_shared<Polling>();
    std::shared_ptr<State> state_;
    std::shared_ptr<Backend> backend_;
    std::shared_ptr<tc::internal::NetworkDelivery> delivery_ = std::make_shared<tc::internal::NetworkDelivery>();
    std::shared_ptr<tc::internal::EventSnapshotDelivery<MidiMessage>::Target> messageTarget_;
    std::string name_;
    int portNumber_ = -1;
    bool virtual_ = false;
    bool ignoreSysex_ = false;
    bool ignoreTiming_ = true;
    bool ignoreSensing_ = true;

};

}  // namespace tcx::midi

namespace tcx { using midi::MidiIn; } // deprecated: remove at v1.0.0
