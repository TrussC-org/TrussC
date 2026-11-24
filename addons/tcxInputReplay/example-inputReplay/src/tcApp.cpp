#include "tcApp.h"

void tcApp::setup() {
    // F9 shows and hides the toolbar. With no argument, no key is assigned.
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    // Optional: start and stop recording from the keyboard (no key by default).
    // replay->setRecordKey(KEY_F8);
    addChild(replay);
}
void tcApp::mousePressed(Vec2 pos, int button) {
    if (replay->isPointerOver()) return;   // the click is for the toolbar
    dots.push_back(pos);
}

void tcApp::draw() {
    clear(0.12f);
    for (auto pos : dots) drawCircle(pos, 6);
}
