// =============================================================================
// videoRecExample - the simplest possible screen recording
// =============================================================================
// This is the empty template, plus one thing: a key that toggles recording.
// startRecording() / stopRecording() are free functions that record the whole
// window to a video file (native encoder, no ffmpeg) — that's all you need.
//
//   any key : start / stop recording  (-> bin/data/recording.mp4)
//
// For codecs, fps, recording an Fbo, or the offline VideoWriter, see
// videoRecAdvancedExample.
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {

}

void tcApp::update() {

}

void tcApp::draw() {
    clear(0.12f);

    // Rotating box
    noFill();
    translate(getWidth() / 2, getHeight() / 2);
    rotate(getElapsedTimef() *0.1f, getElapsedTimef() *0.15f, 0);
    drawBox(200.0f);
}

void tcApp::keyPressed(int key) {
    // The whole point of this example: toggle recording from a key.
    if (isRecording()) stopRecording();
    else               startRecording("recording.mp4");
}

void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(const MouseEventArgs& e) {}
void tcApp::mouseReleased(const MouseEventArgs& e) {}
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {}
void tcApp::mouseScrolled(const ScrollEventArgs& e) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
