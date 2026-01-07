// beepSoundExample - Debug beep sound presets

#include "tcApp.h"

void tcApp::setup() {
}

void tcApp::draw() {
    clear(0.12f);

    setColor(0.7f);
    drawBitmapString(R"(beep() - Debug Sound Presets

[Basic]       1: ping
[Positive]    2: success     3: complete    4: coin
[Negative]    5: error       6: warning     7: cancel
[UI]          8: click       9: typing      0: notify
[Transition]  -: sweep

UP/DOWN: Volume    Click: ping)", 50, 50);

    // Volume bar
    float y = 200;
    setColor(0.5f);
    drawBitmapString(format("Volume: {:.0f}%", getBeepVolume() * 100), 50, y);
    setColor(0.3f);
    drawRect(170, y - 3, 150, 14);
    setColor(colors::lime);
    drawRect(170, y - 3, 150 * getBeepVolume(), 14);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case '1': beep(Beep::ping); break;
        case '2': beep(Beep::success); break;
        case '3': beep(Beep::complete); break;
        case '4': beep(Beep::coin); break;
        case '5': beep(Beep::error); break;
        case '6': beep(Beep::warning); break;
        case '7': beep(Beep::cancel); break;
        case '8': beep(Beep::click); break;
        case '9': beep(Beep::typing); break;
        case '0': beep(Beep::notify); break;
        case '-': beep(Beep::sweep); break;
        case KEY_UP:
            setBeepVolume(getBeepVolume() + 0.1f);
            beep();
            break;
        case KEY_DOWN:
            setBeepVolume(getBeepVolume() - 0.1f);
            beep();
            break;
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    beep();
}
