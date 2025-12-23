// =============================================================================
// videoGrabberExample - Webcam input sample
// =============================================================================
// Simple webcam capture demo.
// Permission is handled automatically - just call setup() and update().
// =============================================================================

#include "tcApp.h"
using namespace std;

void tcApp::setup() {
    // Start camera (if permission not granted, it will be requested automatically)
    grabber_.setup(1280, 720);
}

void tcApp::update() {
    // This also handles pending permission - once granted, camera starts automatically
    grabber_.update();
}

void tcApp::draw() {
    clear(50);

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

    // Draw camera image
    setColor(1.0f);
    if (flipH_) {
        // 左右反転して描画
        pushMatrix();
        translate(grabber_.getWidth(), 0);
        scale(-1, 1);
        grabber_.draw(0, 0);
        popMatrix();
    } else {
        grabber_.draw(0, 0);
    }

    // Display info
    setColor(colors::yellow);
    drawBitmapString(format("{}x{} | FPS: {:.0f} | Flip: {} (F key)",
        grabber_.getWidth(), grabber_.getHeight(), getFrameRate(),
        flipH_ ? "ON" : "OFF"), 10, 20);
}

void tcApp::keyPressed(int key) {
    if (key == 'f' || key == 'F') {
        flipH_ = !flipH_;
    }
}
