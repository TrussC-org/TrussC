// =============================================================================
// videoGrabberExample - Webcam input sample
// =============================================================================
// Simple webcam capture demo.
// Permission is handled automatically - just call setup() and update().
// =============================================================================

#include "tcApp.h"
using namespace std;

void tcApp::setup() {
    // List available cameras
    auto devices = grabber_.listDevices();
    tcLogNotice("tcApp") << "Available cameras:";
    for (const auto& d : devices) {
        tcLogNotice("tcApp") << "  [" << d.deviceId << "] " << d.deviceName;
    }

    // Select camera device (0 = default/first camera)
    grabber_.setDeviceID(0);

    // Start camera (if permission not granted, it will be requested automatically)
    grabber_.setup(1280, 720);
}

void tcApp::update() {
    // This also handles pending permission - once granted, camera starts automatically
    grabber_.update();
}

void tcApp::draw() {
    clear(0.2f);

    if (grabber_.isPendingPermission()) {
        // Waiting for permission
        setColor(1.0f);
        drawBitmapString("Waiting for camera permission...", 20, 30);
        drawBitmapString("Please allow camera access in the dialog.", 20, 50);
        return;
    }

    if (!grabber_.isInitialized()) {
        setColor(1.0f);
        drawBitmapString("Camera not available.", 20, 30);
        return;
    }

    // Draw camera image fitted to window (letterbox/pillarbox)
    float s = std::min(getWindowWidth() / (float)grabber_.getWidth(),
                       getWindowHeight() / (float)grabber_.getHeight());
    float w = grabber_.getWidth() * s;
    float h = grabber_.getHeight() * s;
    float x = (getWindowWidth() - w) / 2;
    float y = (getWindowHeight() - h) / 2;

    setColor(1.0f);
    if (flipH_) {
        // Draw with horizontal flip
        pushMatrix();
        translate(x + w, y);
        scale(-1, 1);
        grabber_.draw(0, 0, w, h);
        popMatrix();
    } else {
        grabber_.draw(x, y, w, h);
    }

    // Display info
    setColor(colors::yellow);
    drawBitmapString(format("{} ({}x{}) | Flip: {} (F key)",
        grabber_.getDeviceName(),
        grabber_.getWidth(), grabber_.getHeight(),
        flipH_ ? "ON" : "OFF"), 10, 20);
}

void tcApp::keyPressed(int key) {
    if (key == 'f' || key == 'F') {
        flipH_ = !flipH_;
    }
}
