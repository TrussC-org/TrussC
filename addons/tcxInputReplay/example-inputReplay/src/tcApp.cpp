#include "tcApp.h"

void tcApp::setup() {
    inputreplay::setup();
}

void tcApp::draw() {
    clear(0.12f);
    drawBitmapString("R: record | S: stop recording | P: play | M: scene mark", 20, 30);
    drawCircle(getMouseX(), getMouseY(), 10);
}

void tcApp::keyPressed(int key) {
    if (key == 'R') inputreplay::startRecording("session.jsonl");
    if (key == 'S') inputreplay::stopRecording();
    if (key == 'P') inputreplay::play("session.jsonl");
    if (key == 'M') inputreplay::mark("scene");
}
