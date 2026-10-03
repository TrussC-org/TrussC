#pragma once

// =============================================================================
// tcxMidiOut.h - MIDI output port (libremidi backend)
// =============================================================================
// Channel numbers are 1-16 to match MidiMessage::getChannel().
//
//   MidiOut out;
//   out.openPort(0);                 // or out.openPort("Launchpad")
//   out.sendNoteOn(1, 60, 100);      // channel 1, middle C, velocity 100
// =============================================================================

#include "tcxMidiMessage.h"

#include "tc/utils/tcLog.h"
#include "tc/utils/tcOnceGate.h"

#include <libremidi/libremidi.hpp>
#include "tcxMidiApi.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tcx::midi {

using namespace tc;

class MidiOut {
public:
    MidiOut() = default;
    ~MidiOut() { closePort(); }

    MidiOut(const MidiOut&) = delete;
    MidiOut& operator=(const MidiOut&) = delete;

    // -------------------------------------------------------------------------
    // Enumeration
    // -------------------------------------------------------------------------
    static std::vector<MidiDeviceInfo> listDevices() {
        std::vector<MidiDeviceInfo> devices;
        libremidi::observer obs{{}, libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_output_ports();
        for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
            devices.push_back({i, ports[i].display_name});
        }
        return devices;
    }

    // -------------------------------------------------------------------------
    // Open / close
    // -------------------------------------------------------------------------
    bool openPort(int index) {
        sendWarnings_.emplace();
        libremidi::observer obs{{}, libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_output_ports();
        if (index < 0 || index >= static_cast<int>(ports.size())) {
            trussc::logError("tcxMidiOut") << "openPort: index " << index
                                           << " out of range (" << ports.size() << " ports)";
            return false;
        }
        return openOutputPort(ports[index], index);
    }

    bool openPort(const std::string& nameContains) {
        sendWarnings_.emplace();
        libremidi::observer obs{{}, libremidi::observer_configuration_for(platformMidiApi())};
        auto ports = obs.get_output_ports();
        for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
            if (ports[i].display_name.find(nameContains) != std::string::npos) {
                return openOutputPort(ports[i], i);
            }
        }
        trussc::logError("tcxMidiOut") << "openPort: no output matching \""
                                       << nameContains << "\"";
        return false;
    }

    bool openVirtualPort(const std::string& name = "TrussC Output") {
        closePort();
        midiOut_ = std::make_unique<libremidi::midi_out>(
            libremidi::output_configuration{}, libremidi::midi_out_configuration_for(platformMidiApi()));
        auto err = midiOut_->open_virtual_port(name);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiOut") << "openVirtualPort failed: " << name;
            midiOut_.reset();
            return false;
        }
        name_ = name;
        portNumber_ = -1;
        virtual_ = true;
        return true;
    }

    void closePort() {
        sendWarnings_.emplace();
        if (midiOut_) {
            midiOut_->close_port();
            midiOut_.reset();
        }
        name_.clear();
        portNumber_ = -1;
        virtual_ = false;
    }

    bool isOpen() const { return midiOut_ && midiOut_->is_port_open(); }
    const std::string& getName() const { return name_; }   // name of the open port
    int  getPort() const { return portNumber_; }           // open port number, -1 if virtual/closed
    bool isVirtual() const { return virtual_; }

    // -------------------------------------------------------------------------
    // Sending
    // -------------------------------------------------------------------------
    // All send helpers return true when libremidi accepts the message. A closed
    // port, empty message or backend error returns false. Warnings are logged
    // once per reason each time the port is opened; closing also resets them.
    // Success does not guarantee delivery to the receiving device.

    // Channel voice messages (channel: 1-16).
    bool sendNoteOn(int channel, int pitch, int velocity) {
        return send3(0x90, channel, pitch, velocity);
    }
    bool sendNoteOff(int channel, int pitch, int velocity = 0) {
        return send3(0x80, channel, pitch, velocity);
    }
    bool sendControlChange(int channel, int control, int value) {
        return send3(0xB0, channel, control, value);
    }
    bool sendProgramChange(int channel, int program) {
        return send2(0xC0, channel, program);
    }
    bool sendAftertouch(int channel, int pressure) {
        return send2(0xD0, channel, pressure);
    }
    bool sendPolyAftertouch(int channel, int pitch, int pressure) {
        return send3(0xA0, channel, pitch, pressure);
    }
    // value: 14-bit (0-16383, center 8192).
    bool sendPitchBend(int channel, int value) {
        value = clamp14(value);
        return send3(0xE0, channel, value & 0x7F, (value >> 7) & 0x7F);
    }
    // Raw 7-bit lsb/msb form (each 0-127).
    bool sendPitchBend(int channel, unsigned char lsb, unsigned char msb) {
        return send3(0xE0, channel, lsb & 0x7F, msb & 0x7F);
    }

    // -------------------------------------------------------------------------
    // Raw / system
    // -------------------------------------------------------------------------
    // Send a full system-exclusive dump (caller includes 0xF0 ... 0xF7).
    bool sendSysex(const std::vector<unsigned char>& bytes) { return sendBytes(bytes); }

    // Send a single raw MIDI byte (e.g. a real-time message like 0xF8 clock).
    bool sendMidiByte(unsigned char byte) { return sendRaw(&byte, 1); }

    // Send arbitrary raw bytes verbatim.
    bool sendBytes(const std::vector<unsigned char>& bytes) {
        return sendRaw(bytes.data(), bytes.size());
    }

    // Send a pre-built MidiMessage.
    bool send(const MidiMessage& msg) { return sendBytes(msg.bytes); }

private:
    static unsigned char chanBits(int channel) {
        int c = channel - 1;
        if (c < 0) c = 0;
        if (c > 15) c = 15;
        return static_cast<unsigned char>(c);
    }
    static int clamp7(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }
    static int clamp14(int v) { return v < 0 ? 0 : (v > 16383 ? 16383 : v); }

    bool send2(unsigned char status, int channel, int d1) {
        const unsigned char bytes[] = {
            static_cast<unsigned char>(status | chanBits(channel)),
            static_cast<unsigned char>(clamp7(d1))};
        return sendRaw(bytes, sizeof(bytes));
    }
    bool send3(unsigned char status, int channel, int d1, int d2) {
        const unsigned char bytes[] = {
            static_cast<unsigned char>(status | chanBits(channel)),
            static_cast<unsigned char>(clamp7(d1)),
            static_cast<unsigned char>(clamp7(d2))};
        return sendRaw(bytes, sizeof(bytes));
    }

    bool sendRaw(const unsigned char* bytes, size_t size) {
        if (!midiOut_) {
            if (sendWarnings_->portClosed.isFirstTime())
                trussc::logWarning("tcxMidiOut") << "send_message failed: no output port is open";
            return false;
        }
        if (size == 0) {
            if (sendWarnings_->emptyMessage.isFirstTime())
                trussc::logWarning("tcxMidiOut") << "send_message failed: empty message";
            return false;
        }
        auto err = midiOut_->send_message(bytes, size);
        if (err != stdx::error{}) {
            if (sendWarnings_->backendError.isFirstTime()) {
                auto message = err.message();
                trussc::logWarning("tcxMidiOut") << "send_message failed: "
                    << std::string(message.data(), message.size());
            }
            return false;
        }
        return true;
    }

    bool openOutputPort(const libremidi::output_port& port, int index) {
        closePort();
        midiOut_ = std::make_unique<libremidi::midi_out>(
            libremidi::output_configuration{}, libremidi::midi_out_configuration_for(platformMidiApi()));
        auto err = midiOut_->open_port(port);
        if (err != stdx::error{}) {
            trussc::logError("tcxMidiOut") << "open_port failed: " << port.display_name;
            midiOut_.reset();
            return false;
        }
        name_ = port.display_name;
        portNumber_ = index;
        virtual_ = false;
        return true;
    }

    struct SendWarnings {
        trussc::OnceGate portClosed;
        trussc::OnceGate emptyMessage;
        trussc::OnceGate backendError;
    };
    // OnceGate is not assignable; emplace recreates all gates without allocation.
    std::optional<SendWarnings> sendWarnings_{std::in_place};
    std::unique_ptr<libremidi::midi_out> midiOut_;
    std::string name_;
    int  portNumber_ = -1;
    bool virtual_ = false;
};

}  // namespace tcx::midi

namespace tcx { using midi::MidiOut; } // deprecated: remove at v1.0.0
