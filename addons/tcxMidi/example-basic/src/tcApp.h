#pragma once

#include <TrussC.h>
#include <tcxMidi.h>

#include <deque>
#include <string>

using namespace tc;
using namespace tcx;
using namespace std;

// =============================================================================
// The simplest tcxMidi usage, and the most universal MIDI tool there is:
//   - open the first input and output port
//   - keep a scrolling history of the last incoming messages
//   - press SPACE to send a note
// Watch the log to learn which control on your device sends what.
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void startMidi();   // open ports once midiReady() (deferred for the web)
    void reconnect();   // reopen a port that was lost, every 2 seconds

    MidiIn  midiIn_;
    MidiOut midiOut_;
    bool    started_ = false;
    bool    hadIn_ = false;    // an input port has been opened once
    bool    hadOut_ = false;
    float   lastTry_ = 0.0f;
    EventListener inLost_, outLost_;

    deque<string> history_;
    static constexpr size_t MAX_LINES = 20;
};
