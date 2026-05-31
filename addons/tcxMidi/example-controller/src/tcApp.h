#pragma once

#include "tcBaseApp.h"
#include <tcxMidi.h>

#include <map>

using namespace tc;
using namespace tcx;
using namespace std;

// =============================================================================
// Generic controller (knob / fader) visualizer.
//
// Knobs and faders send Control Change (CC) messages. The CC *number* assigned
// to each physical control is device-specific, so we don't guess labels - we
// just show a labelled meter for every CC number that appears, and animate it
// to the latest value. Move a knob and its bar shows up automatically.
//
// Input only: opens the first input port.
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    MidiIn midiIn_;
    bool   started_ = false;

    // CC number -> last value (0-127). std::map keeps them sorted by number.
    std::map<int, int> cc_;
};
