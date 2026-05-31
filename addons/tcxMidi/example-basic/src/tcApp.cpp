#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    // On the web, MIDI starts asynchronously - see update()/midiReady().
}

void tcApp::startMidi() {
    // Show what's connected.
    for (auto& d : MidiIn::listDevices())
        logNotice("midi") << "in  [" << d.getPortNumber() << "] " << d.getName();
    for (auto& d : MidiOut::listDevices())
        logNotice("midi") << "out [" << d.getPortNumber() << "] " << d.getName();

    // Open the first available input and output ports.
    midiIn_.openPort(0);
    midiOut_.openPort(0);

    // You can also open a port by (partial) name instead of index:
    //   midiIn_.openPort("Launchpad");
    // Note: some controllers expose several ports - e.g. the Launchpad Mini
    // Mk3 has a "DAW" and a "MIDI" interface, and its pad grid only shows up
    // on the "MIDI" one. Check the names from listDevices() above and
    // match the right one, e.g.:
    //   midiIn_.openPort("LPMiniMK3 MIDI");
}

void tcApp::update() {
    // Open ports once the backend is ready (immediate on native, async on web).
    if (!started_ && midiReady()) { started_ = true; startMidi(); }

    // Drain incoming messages (polling API - runs on the main thread).
    MidiMessage m;
    while (midiIn_.getNextMessage(m)) {
        history_.push_back(m.toString());
        while (history_.size() > MAX_LINES) history_.pop_front();
        logNotice("midi") << history_.back();
    }
}

void tcApp::draw() {
    clear(0.12f);
    setColor(1.0f);

    drawBitmapString("tcxMidi - basic   [SPACE] send NoteOn ch1 note60", 20, 28);
    drawBitmapString("in  : " + (midiIn_.isOpen()  ? midiIn_.getName()  : string("(none)")), 20, 52);
    drawBitmapString("out : " + (midiOut_.isOpen() ? midiOut_.getName() : string("(none)")), 20, 70);

    // Scrolling history of received messages (newest at the bottom).
    setColor(0.55f);
    drawBitmapString("--- incoming ---", 20, 100);
    setColor(0.85f);
    float y = 120;
    for (const auto& line : history_) {
        drawBitmapString(line, 20, y);
        y += 16;
    }
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        // Send a middle-C note (and immediately release it).
        midiOut_.sendNoteOn(1, 60, 100);
        midiOut_.sendNoteOff(1, 60);
    }
}
