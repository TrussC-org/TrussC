// =============================================================================
// videoGrabberExample - Webcam input sample
// =============================================================================
// Simple webcam capture demo.
// Permission is handled automatically - just call setup() and update().
// If the camera is unplugged, onDisconnect fires and update() reopens it with
// setup() (same device, same size) every 2 seconds until it is back.
// =============================================================================

#include "tcApp.h"
using namespace std;

void tcApp::setup() {
    // List available cameras
    auto devices = grabber_.listDevices();
    logNotice("tcApp") << "Available cameras:";
    for (const auto& d : devices) {
        logNotice("tcApp") << "  [" << d.deviceId << "] " << d.deviceName;
    }

    // Select camera device (0 = default/first camera)
    grabber_.setDeviceID(0);

    // The OS reported the camera lost (unplugged). The grabber has closed
    // itself; update() below reconnects.
    lostListener_ = grabber_.onDisconnect.listen([](DeviceDisconnectEventArgs& e) {
        logWarning("tcApp") << "Camera lost: " << e.reason;
    });

    // Start camera (if permission not granted, it will be requested automatically)
    grabber_.setup(1280, 720);
}

void tcApp::update() {
    // This also handles pending permission - once granted, camera starts automatically
    // This also reports a lost camera (onDisconnect)
    grabber_.update();

    // Not connected (never was, or the camera was lost): try again every 2
    // seconds. setup() with no arguments reopens the same device at the last
    // size. Same as `if (!grabber_.isConnected() && timer) grabber_.setup();`
    if (!grabber_.isConnected() && !grabber_.isPendingPermission() &&
        getElapsedTime() - lastTry_ > 2.0f) {
        lastTry_ = getElapsedTime();
        grabber_.setup();
    }
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

    if (!grabber_.isConnected()) {
        setColor(1.0f);
        drawBitmapString("Camera not available. Trying again every 2 seconds...", 20, 30);
        return;
    }

    // Draw camera image fitted to window (letterbox/pillarbox)
    float s = std::min(getWidth() / (float)grabber_.getWidth(),
                       getHeight() / (float)grabber_.getHeight());
    float w = grabber_.getWidth() * s;
    float h = grabber_.getHeight() * s;
    float x = (getWidth() - w) / 2;
    float y = (getHeight() - h) / 2;

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
    if (key == 'F') {
        flipH_ = !flipH_;
    }
}
