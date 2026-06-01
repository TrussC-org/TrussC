#include "tcApp.h"

void tcApp::setup() {

}

void tcApp::update() {

}

void tcApp::draw() {
    clear(0.12f);

    // Rotating box
    noFill();
    translate(getWindowWidth() / 2, getWindowHeight() / 2);
    rotate(getElapsedTimef() *0.1f, getElapsedTimef() *0.15f, 0);
    drawBox(200.0f);
}

void tcApp::keyPressed(int key) {}
void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(const MouseEventArgs& e) {}
void tcApp::mouseReleased(const MouseEventArgs& e) {}
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {}
void tcApp::mouseScrolled(const ScrollEventArgs& e) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
