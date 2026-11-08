#include "tcApp.h"

void tcApp::setup() {
    // Keep the listener in a member so it stays subscribed.
    afterFrameListener_ = events().afterFrame.listen([this]() {
        if (!grabRequested_) return;
        grabRequested_ = false;
        // The frame is finished here, including ImGui if the app uses it.
        // Drawing to the screen in afterFrame will not appear: only read back
        // the frame or render into an Fbo. Show the uploaded texture in draw().
        if (grabScreen(pixels_)) thumbnail_.allocate(pixels_);
    });
}

void tcApp::draw() {
    clear(0.08f, 0.1f, 0.14f);
    setColor(1);
    drawBitmapString("Space: grab the finished frame", 30, 35);
    drawBitmapString("The thumbnail appears on the next frame.", 30, 60);

    setColor(1, 0, 0);
    drawRect(40, 110, 180, 140);
    setColor(0.2f, 0.8f, 0.7f);
    drawCircle(310, 180, 65);
    float angle = getElapsedTimef() * TAU / 4;
    setColor(1, 0.8f, 0.2f);
    drawCircle(310 + cos(angle) * 45, 180 + sin(angle) * 45, 12);

    if (thumbnail_.isAllocated()) {
        setColor(1);
        float x = getWidth() >= 770 ? 440 : 30;
        float y = getWidth() >= 770 ? 115 : 300;
        float width = min(300.0f, min(getWidth() - x - 30, (getHeight() - y - 30) *
                          thumbnail_.getWidth() / thumbnail_.getHeight()));
        drawBitmapString("last grab: " + to_string(thumbnail_.getWidth()) + " x " +
                         to_string(thumbnail_.getHeight()) + " pixels", x, y - 15);
        float height = width * thumbnail_.getHeight() / thumbnail_.getWidth();
        thumbnail_.draw(x, y, width, height);
    }
}

void tcApp::keyPressed(int key) {
    if (key == ' ') grabRequested_ = true;
}
