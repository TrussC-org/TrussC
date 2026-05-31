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

#include <libremidi/libremidi.hpp>

#include <memory>
#include <string>
#include <vector>

namespace tcx {

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
        libremidi::observer obs;
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
        libremidi::observer obs;
        auto ports = obs.get_output_ports();
        if (index < 0 || index >= static_cast<int>(ports.size())) {
            trussc::logError("tcxMidiOut") << "openPort: index " << index
                                           << " out of range (" << ports.size() << " ports)";
            return false;
        }
        return openOutputPort(ports[index], index);
    }

    bool openPort(const std::string& nameContains) {
        libremidi::observer obs;
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
        midiOut_ = std::make_unique<libremidi::midi_out>();
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
    // Channel voice messages (channel: 1-16)
    // -------------------------------------------------------------------------
    void sendNoteOn(int channel, int pitch, int velocity) {
        send3(0x90, channel, pitch, velocity);
    }
    void sendNoteOff(int channel, int pitch, int velocity = 0) {
        send3(0x80, channel, pitch, velocity);
    }
    void sendControlChange(int channel, int control, int value) {
        send3(0xB0, channel, control, value);
    }
    void sendProgramChange(int channel, int program) {
        send2(0xC0, channel, program);
    }
    void sendAftertouch(int channel, int pressure) {
        send2(0xD0, channel, pressure);
    }
    void sendPolyAftertouch(int channel, int pitch, int pressure) {
        send3(0xA0, channel, pitch, pressure);
    }
    // value: 14-bit (0-16383, center 8192).
    void sendPitchBend(int channel, int value) {
        if (!midiOut_) return;
        value = clamp14(value);
        midiOut_->send_message(static_cast<unsigned char>(0xE0 | chanBits(channel)),
                               static_cast<unsigned char>(value & 0x7F),
                               static_cast<unsigned char>((value >> 7) & 0x7F));
    }
    // Raw 7-bit lsb/msb form (each 0-127).
    void sendPitchBend(int channel, unsigned char lsb, unsigned char msb) {
        if (!midiOut_) return;
        midiOut_->send_message(static_cast<unsigned char>(0xE0 | chanBits(channel)),
                               static_cast<unsigned char>(lsb & 0x7F),
                               static_cast<unsigned char>(msb & 0x7F));
    }

    // -------------------------------------------------------------------------
    // Raw / system
    // -------------------------------------------------------------------------
    // Send a full system-exclusive dump (caller includes 0xF0 ... 0xF7).
    void sendSysex(const std::vector<unsigned char>& bytes) { sendBytes(bytes); }

    // Send a single raw MIDI byte (e.g. a real-time message like 0xF8 clock).
    void sendMidiByte(unsigned char byte) {
        if (!midiOut_) return;
        midiOut_->send_message(&byte, 1);
    }

    // Send arbitrary raw bytes verbatim.
    void sendBytes(const std::vector<unsigned char>& bytes) {
        if (!midiOut_ || bytes.empty()) return;
        midiOut_->send_message(bytes.data(), bytes.size());
    }

    // Send a pre-built MidiMessage.
    void send(const MidiMessage& msg) { sendBytes(msg.bytes); }

private:
    static unsigned char chanBits(int channel) {
        int c = channel - 1;
        if (c < 0) c = 0;
        if (c > 15) c = 15;
        return static_cast<unsigned char>(c);
    }
    static int clamp7(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }
    static int clamp14(int v) { return v < 0 ? 0 : (v > 16383 ? 16383 : v); }

    void send2(unsigned char status, int channel, int d1) {
        if (!midiOut_) return;
        midiOut_->send_message(static_cast<unsigned char>(status | chanBits(channel)),
                               static_cast<unsigned char>(clamp7(d1)));
    }
    void send3(unsigned char status, int channel, int d1, int d2) {
        if (!midiOut_) return;
        midiOut_->send_message(static_cast<unsigned char>(status | chanBits(channel)),
                               static_cast<unsigned char>(clamp7(d1)),
                               static_cast<unsigned char>(clamp7(d2)));
    }

    bool openOutputPort(const libremidi::output_port& port, int index) {
        closePort();
        midiOut_ = std::make_unique<libremidi::midi_out>();
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

    std::unique_ptr<libremidi::midi_out> midiOut_;
    std::string name_;
    int  portNumber_ = -1;
    bool virtual_ = false;
};

}  // namespace tcx
