#pragma once

// =============================================================================
// tcxMidiClock.h - MIDI beat clock tracker (counterpart to ofxMidiClock)
// =============================================================================
// Feed every incoming message to update(); it picks out timing-clock (0xF8)
// and song-position (0xF2) messages and tracks tempo + song position.
//
//   MidiClock clock;
//   midiIn.onMessage.listen([&](MidiMessage& m){ clock.update(m); });
//   ... clock.getBpm(), clock.getBeats(), clock.getSeconds() ...
//
// "Beats" here are MIDI beats = 16th notes = 6 clock ticks (24 PPQN), matching
// the Song Position Pointer unit. Ported from danomatika/ofxMidi (BSD).
// =============================================================================

#include "tcxMidiMessage.h"

#include <chrono>

namespace tcx::midi {

using namespace tc;

class MidiClock {
public:
    MidiClock() { reset(); }

    // Returns true if the message was a clock-related message we consumed.
    bool update(const MidiMessage& m) {
        if (m.bytes.empty()) return false;
        switch (m.bytes[0]) {
            case 0xF8:  // timing clock
                tick();
                return true;
            case 0xF2:  // song position pointer (14-bit, in MIDI beats)
                if (m.bytes.size() < 3) return false;
                ticks_ = (static_cast<unsigned>(m.bytes[2]) << 7 | m.bytes[1]) * 6;
                return true;
        }
        return false;
    }

    // Advance one clock tick, updating the averaged tick length.
    void tick() {
        auto now = std::chrono::steady_clock::now();
        double us = std::chrono::duration_cast<std::chrono::microseconds>(
                        now - timestamp_).count();
        if (us < 200000.0) {  // filter obviously bad gaps (> 200ms)
            length_ += ((us / 1000.0) - length_) / 5.0;  // average last ~5 ticks
            ticks_++;
        }
        timestamp_ = now;
    }

    void reset() {
        timestamp_ = std::chrono::steady_clock::now();
        ticks_ = 0;
    }

    // Song position in MIDI beats (16th notes).
    unsigned int getBeats() const { return static_cast<unsigned int>(ticks_ / 6); }
    void setBeats(unsigned int beats) { ticks_ = beats * 6; }

    double getSeconds() const { return beatsToSeconds(static_cast<unsigned int>(ticks_ / 6)); }
    void   setSeconds(double s) { ticks_ = secondsToBeats(s) * 6; }

    double getBpm() const { return msToBpm(length_); }
    void   setBpm(double bpm) { length_ = bpmToMs(bpm); }

    double       beatsToSeconds(unsigned int beats) const { return (double)beats * length_ * 0.006; }
    unsigned int secondsToBeats(double seconds) const { return (unsigned int)((seconds * 1000) / (6 * length_)); }

    // length here is the per-tick length in ms (24 ticks per quarter note).
    static double bpmToMs(double bpm) { return bpm == 0 ? 0 : 2500.0 / bpm; }
    static double msToBpm(double ms) { return ms == 0 ? 0 : 2500.0 / ms; }

private:
    double        length_ = 20.833;  // averaged tick length in ms (default 120 BPM)
    unsigned long ticks_  = 0;        // song position in ticks (6 ticks = 1 beat)
    std::chrono::steady_clock::time_point timestamp_;  // last tick time
};

}  // namespace tcx::midi

namespace tcx { using midi::MidiClock; } // deprecated: remove at v1.0.0
