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
        if (m.isControlChange()) {
            cc_[m.getControl()] = m.getValue();  // adds a new row the first time
        }
    }
}

void tcApp::draw() {
    clear(0.1f);

    setColor(0.85f);
    drawBitmapString("tcxMidi - controller   in: " +
                     (midiIn_.isOpen() ? midiIn_.getName() : string("(none)")), 16, 24);
    setColor(0.5f);
    drawBitmapString("move a knob/fader - a meter appears per CC number", 16, 44);

    if (cc_.empty()) return;

    const float x      = 16.0f;
    const float labelW = 90.0f;       // room for "CC 127: 127"
    const float barX   = x + labelW;
    const float barW   = getWindowWidth() - barX - 16.0f;
    const float rowH   = 24.0f;
    float y = 68.0f;

    for (const auto& [num, value] : cc_) {
        // Label
        setColor(0.8f);
        drawBitmapString("CC" + to_string(num) + ":" + to_string(value), x, y + 12);

        // Track + filled meter
        setColor(0.2f);
        drawRect(barX, y, barW, rowH - 6);
        setColor(0.3f, 0.7f, 1.0f);
        drawRect(barX, y, barW * (value / 127.0f), rowH - 6);

        y += rowH;
    }
}
