#pragma once

// =============================================================================
// tcxMidiMessage.h - A single parsed MIDI message
// =============================================================================
// Lightweight, copyable value type. Holds the raw bytes plus convenience
// accessors whose meaning depends on the message type (oF-style API).
//
// Channel numbers are 1-16 (0 for system messages), matching most DAWs.
// =============================================================================

#include "tcxMidiConstants.h"

#include <string>
#include <vector>
#include <cstdint>

namespace tcx {

struct MidiMessage {
    std::vector<unsigned char> bytes;  // raw MIDI bytes
    double deltatime = 0.0;            // seconds since the previous event (input)
    std::string portName;              // device the message came from (input only)
    int portNum = -1;                  // port number it came from (-1 if virtual)

    MidiMessage() = default;
    MidiMessage(std::vector<unsigned char> b, double t = 0.0)
        : bytes(std::move(b)), deltatime(t) {}

    bool empty() const { return bytes.empty(); }
    size_t size() const { return bytes.size(); }

    // -------------------------------------------------------------------------
    // Type queries
    // -------------------------------------------------------------------------

    // High nibble for channel messages, full status byte for system messages
    // (so clock / start / stop / SPP / MTC are distinguishable, not all Sysex).
    MidiStatus getStatus() const {
        if (bytes.empty()) return MidiStatus::Unknown;
        uint8_t s = bytes[0];
        if (s >= 0xF0) return static_cast<MidiStatus>(s);   // exact system status
        return static_cast<MidiStatus>(s & 0xF0);            // channel message
    }

    // 1-16 for channel messages, 0 for system messages.
    int getChannel() const {
        if (bytes.empty()) return 0;
        uint8_t s = bytes[0];
        if ((s & 0xF0) == 0xF0) return 0;
        return (s & 0x0F) + 1;
    }

    bool isNoteOn() const {
        // A NoteOn with velocity 0 is conventionally a NoteOff.
        return getStatus() == MidiStatus::NoteOn && getVelocity() > 0;
    }
    bool isNoteOff() const {
        if (getStatus() == MidiStatus::NoteOff) return true;
        return getStatus() == MidiStatus::NoteOn && getVelocity() == 0;
    }
    bool isControlChange()  const { return getStatus() == MidiStatus::ControlChange; }
    bool isProgramChange()  const { return getStatus() == MidiStatus::ProgramChange; }
    bool isPitchBend()      const { return getStatus() == MidiStatus::PitchBend; }
    bool isAftertouch()     const { return getStatus() == MidiStatus::Aftertouch; }
    bool isPolyAftertouch() const { return getStatus() == MidiStatus::PolyAftertouch; }
    bool isSysex()          const { return getStatus() == MidiStatus::Sysex; }

    // System real-time / common messages
    bool isClock()         const { return getStatus() == MidiStatus::TimeClock; }
    bool isStart()         const { return getStatus() == MidiStatus::Start; }
    bool isContinue()      const { return getStatus() == MidiStatus::Continue; }
    bool isStop()          const { return getStatus() == MidiStatus::Stop; }
    bool isActiveSensing() const { return getStatus() == MidiStatus::ActiveSensing; }
    bool isSongPosition()  const { return getStatus() == MidiStatus::SongPosPointer; }
    bool isTimeCode()      const { return getStatus() == MidiStatus::TimeCode; }

    // -------------------------------------------------------------------------
    // Data accessors (meaning depends on the message type)
    // -------------------------------------------------------------------------

    // Note number for NoteOn/NoteOff/PolyAftertouch (0-127).
    int getPitch() const { return byteAt(1); }

    // Velocity for NoteOn/NoteOff (0-127).
    int getVelocity() const { return byteAt(2); }

    // Controller number for ControlChange (0-127).
    int getControl() const { return byteAt(1); }

    // Generic 2nd data byte: CC value, channel pressure, etc. (0-127).
    // For ProgramChange this is the program number (1st data byte).
    int getValue() const {
        if (getStatus() == MidiStatus::ProgramChange ||
            getStatus() == MidiStatus::Aftertouch) {
            return byteAt(1);
        }
        return byteAt(2);
    }

    // Pitch bend as a 14-bit value (0-16383, center 8192).
    int getPitchBend() const {
        if (bytes.size() < 3) return MIDI_PITCH_BEND_CENTER;
        return (bytes[1] & 0x7F) | ((bytes[2] & 0x7F) << 7);
    }

    // Song Position Pointer as a 14-bit value, in MIDI beats (16th notes).
    int getSongPosition() const {
        if (bytes.size() < 3) return 0;
        return (bytes[1] & 0x7F) | ((bytes[2] & 0x7F) << 7);
    }

    // -------------------------------------------------------------------------
    // Debug
    // -------------------------------------------------------------------------
    std::string toString() const {
        std::string out = tcx::toString(getStatus());
        if (getChannel() > 0) out += " ch" + std::to_string(getChannel());
        switch (getStatus()) {
            case MidiStatus::NoteOn:
            case MidiStatus::NoteOff:
                out += " note=" + std::to_string(getPitch()) +
                       " vel=" + std::to_string(getVelocity());
                break;
            case MidiStatus::ControlChange:
                out += " cc=" + std::to_string(getControl()) +
                       " val=" + std::to_string(getValue());
                break;
            case MidiStatus::ProgramChange:
                out += " prog=" + std::to_string(getValue());
                break;
            case MidiStatus::PitchBend:
                out += " bend=" + std::to_string(getPitchBend());
                break;
            case MidiStatus::Sysex:
                out += " (" + std::to_string(bytes.size()) + " bytes)";
                break;
            default:
                break;
        }
        return out;
    }

    // Name of a status byte (e.g. for logging). Matches ofxMidi's static helper.
    static std::string getStatusString(MidiStatus status) {
        return tcx::toString(status);
    }

private:
    int byteAt(size_t i) const { return i < bytes.size() ? bytes[i] : 0; }
};

}  // namespace tcx
