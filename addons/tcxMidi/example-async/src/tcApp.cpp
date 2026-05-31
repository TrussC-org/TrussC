#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    // Attach the listener now; the callback runs on the MIDI input thread.
    // (Member-function style also works: onMessage.listen(this, &tcApp::onMidi).)
    listener_ = midiIn_.onMessage.listen([this](MidiMessage& m) {
        // *** MIDI thread - not the main thread. ***
        // Touch shared data only under the lock; never draw here.
        {
            lock_guard<mutex> lock(historyMutex_);
            history_.push_back(m.toString());
            while (history_.size() > MAX_LINES) history_.pop_front();
        }
        received_++;  // atomic, so the counter is fine to bump lock-free
    });
    // The port is opened in update() once midiReady() (deferred on the web).
}

void tcApp::update() {
    if (!started_ && midiReady()) {
        started_ = true;
        for (auto& d : MidiIn::listDevices())
            logNotice("midi") << "in [" << d.getPortNumber() << "] " << d.getName();
        midiIn_.openPort(0);
    }
}

void tcApp::draw() {
    clear(0.12f);
    setColor(1.0f);
    drawBitmapString("tcxMidi - async (callback on MIDI thread, mutex-guarded)", 16, 28);
    drawBitmapString("in: " + (midiIn_.isOpen() ? midiIn_.getName() : string("(none)")), 16, 52);
    drawBitmapString("received: " + to_string(received_.load()), 16, 70);

    setColor(0.55f);
    drawBitmapString("--- incoming (newest at bottom) ---", 16, 98);
    setColor(0.85f);
    {
        lock_guard<mutex> lock(historyMutex_);
        float y = 118;
        for (const auto& line : history_) {
            drawBitmapString(line, 16, y);
            y += 16;
        }
    }
}

void tcApp::cleanup() {
    // Stop the input thread before teardown so the callback can't fire late.
    listener_.disconnect();
    midiIn_.closePort();
}
