#pragma once

// =============================================================================
// tcxMidiConstants.h - MIDI status types and helpers
// =============================================================================

#include <cstdint>
#include <string>

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>
#endif

namespace tcx {

// ---------------------------------------------------------------------------
// midiReady() - is the MIDI backend ready to enumerate/open ports?
// ---------------------------------------------------------------------------
// Native backends are ready immediately. On the Web (Emscripten) build, Web
// MIDI is asynchronous: navigator.requestMIDIAccess() resolves a frame or more
// after launch, and touching libremidi before then crashes. Call this each
// frame and only enumerate / open ports once it returns true:
//
//     if (!started_ && midiReady()) { started_ = true; midiIn.openPort(0); }
//
// Note: on the web the access is requested WITHOUT sysex, so sysex send/receive
// (e.g. Launchpad Programmer mode) is native-only. WebMIDI also needs a
// Chromium-based browser served over https.
inline bool midiReady() {
#if defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        if (typeof globalThis.__libreMidi_access === 'undefined') {
            // Kick the async request off once (matches libremidi's own global).
            if (navigator.requestMIDIAccess && !globalThis.__tcxMidiRequested) {
                globalThis.__tcxMidiRequested = true;
                navigator.requestMIDIAccess().then(
                    function(a) { globalThis.__libreMidi_access = a; },
                    function()  { console.log('tcxMidi: WebMIDI unavailable or denied'); });
            }
            return 0;
        }
        return 1;
    });
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// MidiDeviceInfo - one entry from MidiIn::listDevices() / MidiOut::listDevices()
// Mirrors the TrussC convention of AudioDeviceInfo / VideoDeviceInfo.
// ---------------------------------------------------------------------------
struct MidiDeviceInfo {
    int         portNumber = -1;  // pass to openPort(int)
    std::string name;             // display name; pass to openPort(string)

    int                getPortNumber() const { return portNumber; }
    const std::string& getName()       const { return name; }
};

// MIDI message status (high nibble of the status byte for channel messages,
// full byte for system messages). Mirrors libremidi::message_type but kept
// as a TrussC-side enum so user code never needs to touch libremidi headers.
enum class MidiStatus : uint8_t {
    Unknown        = 0x00,

    // Channel voice messages (low nibble = channel)
    NoteOff        = 0x80,
    NoteOn         = 0x90,
    PolyAftertouch = 0xA0,  // polyphonic key pressure
    ControlChange  = 0xB0,
    ProgramChange  = 0xC0,
    Aftertouch     = 0xD0,  // channel pressure
    PitchBend      = 0xE0,

    // System common messages
    Sysex          = 0xF0,  // system exclusive start (0xF0 ... 0xF7)
    TimeCode       = 0xF1,  // MTC quarter frame
    SongPosPointer = 0xF2,  // song position pointer (14-bit, in MIDI beats)
    SongSelect     = 0xF3,
    TuneRequest    = 0xF6,
    SysexEnd       = 0xF7,  // end of exclusive

    // System real-time messages
    TimeClock      = 0xF8,  // timing clock (24 per quarter note)
    Start          = 0xFA,
    Continue       = 0xFB,
    Stop           = 0xFC,
    ActiveSensing  = 0xFE,
    SystemReset    = 0xFF,
};

// 14-bit pitch bend center value (no bend).
constexpr int MIDI_PITCH_BEND_CENTER = 8192;

// Human-readable name for a status, handy for logging / monitors.
inline const char* toString(MidiStatus s) {
    switch (s) {
        case MidiStatus::NoteOff:        return "NoteOff";
        case MidiStatus::NoteOn:         return "NoteOn";
        case MidiStatus::PolyAftertouch: return "PolyAftertouch";
        case MidiStatus::ControlChange:  return "ControlChange";
        case MidiStatus::ProgramChange:  return "ProgramChange";
        case MidiStatus::Aftertouch:     return "Aftertouch";
        case MidiStatus::PitchBend:      return "PitchBend";
        case MidiStatus::Sysex:          return "Sysex";
        case MidiStatus::TimeCode:       return "TimeCode";
        case MidiStatus::SongPosPointer: return "SongPosPointer";
        case MidiStatus::SongSelect:     return "SongSelect";
        case MidiStatus::TuneRequest:    return "TuneRequest";
        case MidiStatus::SysexEnd:       return "SysexEnd";
        case MidiStatus::TimeClock:      return "TimeClock";
        case MidiStatus::Start:          return "Start";
        case MidiStatus::Continue:       return "Continue";
        case MidiStatus::Stop:           return "Stop";
        case MidiStatus::ActiveSensing:  return "ActiveSensing";
        case MidiStatus::SystemReset:    return "SystemReset";
        default:                         return "Unknown";
    }
}

}  // namespace tcx
