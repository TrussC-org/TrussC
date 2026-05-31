#pragma once

#include "tcBaseApp.h"
#include <tcxMidi.h>

#include <array>

using namespace tc;
using namespace tcx;
using namespace std;

// =============================================================================
// Generic MIDI keyboard visualizer.
//
// MIDI note numbers are standardised pitches (middle C = 60), so this works
// the same on ANY keyboard. Held keys light up; velocity sets the brightness.
// (Drum pads send notes too, so they light scattered keys - that's expected.)
//
// Input only: opens the first input port, no output assumptions.
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    static bool isWhite(int note) {
        switch (note % 12) {
            case 0: case 2: case 4: case 5: case 7: case 9: case 11: return true;
            default: return false;
        }
    }

    MidiIn midiIn_;
    bool   started_ = false;

    static constexpr int LO = 21;   // A0
    static constexpr int HI = 108;  // C8 (88-key piano range)
    std::array<bool, 128> held_{};
    std::array<int, 128>  vel_{};
};
