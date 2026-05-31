#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    // MIDI starts in update() once midiReady() (deferred on the web).
}

void tcApp::update() {
    if (!started_ && midiReady()) {
        started_ = true;
        for (auto& d : MidiIn::listDevices())
            logNotice("midi") << "in [" << d.getPortNumber() << "] " << d.getName();
        midiIn_.openPort(0);
    }

    MidiMessage m;
    while (midiIn_.getNextMessage(m)) {
        int n = m.getPitch();
        if (n < 0 || n > 127) continue;
        if (m.isNoteOn())  { held_[n] = true;  vel_[n] = m.getVelocity(); }
        if (m.isNoteOff()) { held_[n] = false; }
    }
}

void tcApp::draw() {
    clear(0.1f);

    setColor(0.85f);
    drawBitmapString("tcxMidi - keyboard   in: " +
                     (midiIn_.isOpen() ? midiIn_.getName() : string("(none)")), 16, 24);

    // Count white keys in range so they fill the window width.
    int whiteCount = 0;
    for (int n = LO; n <= HI; ++n) if (isWhite(n)) whiteCount++;

    const float margin = 16.0f;
    const float top    = 48.0f;
    const float whiteW = (getWindowWidth() - 2 * margin) / whiteCount;
    const float whiteH = getWindowHeight() - top - 16.0f;
    const float blackW = whiteW * 0.62f;
    const float blackH = whiteH * 0.62f;

    // Map each white note to an x position (used to place black keys too).
    std::array<float, 128> whiteX{};
    {
        int w = 0;
        for (int n = LO; n <= HI; ++n) {
            if (isWhite(n)) { whiteX[n] = margin + w * whiteW; ++w; }
        }
    }

    // White keys.
    for (int n = LO; n <= HI; ++n) {
        if (!isWhite(n)) continue;
        if (held_[n]) setColor(0.2f, 0.4f + 0.6f * vel_[n] / 127.0f, 0.3f);
        else          setColor(0.95f);
        drawRect(whiteX[n], top, whiteW - 1.5f, whiteH);

        // Octave label under each C.
        if (n % 12 == 0) {
            setColor(0.45f);
            drawBitmapString("C" + to_string(n / 12 - 1), whiteX[n] + 3, top + whiteH + 12);
        }
    }

    // Black keys, drawn on top straddling the boundary above their lower white.
    for (int n = LO; n <= HI; ++n) {
        if (isWhite(n)) continue;
        float bx = whiteX[n - 1] + whiteW - blackW * 0.5f;
        if (held_[n]) setColor(0.3f, 0.5f + 0.5f * vel_[n] / 127.0f, 0.4f);
        else          setColor(0.08f);
        drawRect(bx, top, blackW, blackH);
    }
}
