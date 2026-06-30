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
//      getNextMessage() call.
// =============================================================================

#include "tcxMidiMessage.h"

#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcLog.h"

#include <libremidi/libremidi.hpp>
#include "tcxMidiApi.h"

#include <atomic>
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

    MidiIn() = default;
    ~MidiIn() { closePort(); }

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
        midiIn_ = std::make_unique<libremidi::midi_in>(
            makeConfig(), libremidi::midi_in_configuration_for(platformMidiApi()));
        auto err = midiIn_->open_virtual_port(name);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiIn") << "openVirtualPort failed: " << name;
            midiIn_.reset();
            return false;
        }
        name_ = name;
        portNumber_ = -1;
        virtual_ = true;
        return true;
    }

    void closePort() {
        if (midiIn_) {
            midiIn_->close_port();
            midiIn_.reset();
        }
        name_.clear();
        portNumber_ = -1;
        virtual_ = false;
    }

    bool isOpen() const { return midiIn_ && midiIn_->is_port_open(); }
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

    // Enables the internal queue on first call. Returns true if unread.
    bool hasNewMessage() {
        bufferEnabled_ = true;
        std::lock_guard<std::mutex> lock(queueMutex_);
        return !queue_.empty();
    }

    // Pop the next queued message. Returns false when the queue is empty.
    bool getNextMessage(MidiMessage& msg) {
        bufferEnabled_ = true;
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.empty()) return false;
        msg = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void setBufferSize(size_t size) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        bufferMax_ = size;
        while (queue_.size() > bufferMax_) queue_.pop();
    }
    size_t getBufferSize() const { return bufferMax_; }

private:
    libremidi::input_configuration makeConfig() {
        libremidi::input_configuration cfg;
        cfg.on_message = [this](const libremidi::message& m) { handleMessage(m); };
        cfg.ignore_sysex = ignoreSysex_;
        cfg.ignore_timing = ignoreTiming_;
        cfg.ignore_sensing = ignoreSensing_;
        // Relative mode: timestamp = ns since the previous event, so our
        // MidiMessage::deltatime carries a delta as the name implies.
        cfg.timestamps = libremidi::timestamp_mode::Relative;
        return cfg;
    }

    bool openInputPort(const libremidi::input_port& port, int index) {
        closePort();
        midiIn_ = std::make_unique<libremidi::midi_in>(
            makeConfig(), libremidi::midi_in_configuration_for(platformMidiApi()));
        auto err = midiIn_->open_port(port);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiIn") << "open_port failed: " << port.display_name;
            midiIn_.reset();
            return false;
        }
        name_ = port.display_name;
        portNumber_ = index;
        virtual_ = false;
        return true;
    }

    void handleMessage(const libremidi::message& m) {
        MidiMessage msg;
        msg.bytes.assign(m.bytes.begin(), m.bytes.end());
        msg.deltatime = static_cast<double>(m.timestamp) * 1e-9;  // ns -> s
        msg.portName = name_;
        msg.portNum = portNumber_;

        if (bufferEnabled_) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.push(msg);
            while (queue_.size() > bufferMax_) queue_.pop();
        }
        onMessage.notify(msg);
    }

    std::unique_ptr<libremidi::midi_in> midiIn_;
    std::string name_;
    int  portNumber_ = -1;
    bool virtual_ = false;

    bool ignoreSysex_ = false;
    bool ignoreTiming_ = true;
    bool ignoreSensing_ = true;

    std::queue<MidiMessage> queue_;
    std::mutex queueMutex_;
    std::atomic<bool> bufferEnabled_{false};
    size_t bufferMax_ = 256;
};

}  // namespace tcx::midi

namespace tcx { using midi::MidiIn; } // deprecated: remove at v1.0.0
