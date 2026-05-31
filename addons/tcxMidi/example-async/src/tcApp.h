#pragma once

#include "tcBaseApp.h"
#include <tcxMidi.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

using namespace tc;
using namespace tcx;
using namespace std;

// =============================================================================
// Asynchronous input (event callback on the MIDI thread).
//
// Instead of polling in update(), we attach a listener to midiIn_.onMessage.
// The callback fires on libremidi's input thread the instant a message
// arrives - NOT on the main (update/draw) thread.
//
//   [!] Rules for the callback:
//       - guard any data shared with draw()/update() using a mutex
//       - do NOT draw or call any graphics/GL from it (main thread only)
//       - keep it short
//
// Compare with example-basic, which polls the same data on the main thread.
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

private:
    MidiIn midiIn_;
    bool   started_ = false;

    // Shared between the MIDI thread (writer) and draw() (reader).
    mutex             historyMutex_;
    deque<string>     history_;
    atomic<int>       received_{0};
    static constexpr size_t MAX_LINES = 20;

    // Keep the listener alive (it disconnects when destroyed). Declared last
    // so it is destroyed first - before midiIn_ / the shared data it touches.
    EventListener listener_;
};
