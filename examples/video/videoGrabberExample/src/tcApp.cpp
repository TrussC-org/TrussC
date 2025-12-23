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
    grabber_.setup(640, 480);
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
    grabber_.draw(0, 0);

    // Display info
    setColor(colors::yellow);
    drawBitmapString(format("{}x{} | FPS: {:.0f}",
        grabber_.getWidth(), grabber_.getHeight(), getFrameRate()), 10, 20);
}
