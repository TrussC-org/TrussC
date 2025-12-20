// =============================================================================
// videoGrabberExample - Webcam input sample
// =============================================================================

#include "tcApp.h"

using namespace std;
using namespace tc;

void tcApp::setup() {
    // Check camera permission
    permissionGranted_ = VideoGrabber::checkCameraPermission();

    if (permissionGranted_) {
        // Get list of available cameras
        devices_ = grabber_.listDevices();
        tcLogNotice("tcApp") << "=== Available Cameras ===";
        for (auto& dev : devices_) {
            tcLogNotice("tcApp") << "[" << dev.deviceId << "] " << dev.deviceName;
        }
        tcLogNotice("tcApp") << "========================";
        tcLogNotice("tcApp") << "";
        tcLogNotice("tcApp") << "Press 1-9 to switch camera, SPACE to restart current camera";

        // Start capture with default camera
        if (!devices_.empty()) {
            currentDevice_ = 0;
            grabber_.setDeviceID(currentDevice_);
            grabber_.setVerbose(true);  // Enable verbose logging
            // grabber_.setDesiredFrameRate(30);  // Set frame rate (optional)
            if (grabber_.setup(640, 480)) {
                tcLogNotice("tcApp") << "Camera started: " << grabber_.getWidth() << "x" << grabber_.getHeight()
                              << " (" << grabber_.getDeviceName() << ")";
            } else {
                tcLogError("tcApp") << "Failed to start camera";
            }
        }
    } else {
        tcLogWarning("tcApp") << "Camera permission not granted. Requesting...";
        VideoGrabber::requestCameraPermission();
        permissionRequested_ = true;
    }
}

void tcApp::update() {
    // Re-check if permission not yet granted
    if (!permissionGranted_ && permissionRequested_) {
        permissionGranted_ = VideoGrabber::checkCameraPermission();
        if (permissionGranted_) {
            // Start camera once permission is granted
            devices_ = grabber_.listDevices();
            if (!devices_.empty()) {
                grabber_.setDeviceID(0);
                grabber_.setup(640, 480);
                tcLogNotice("tcApp") << "Permission granted! Camera started.";
            }
        }
    }

    // Update camera frame
    grabber_.update();

    frameCount_++;
    if (grabber_.isFrameNew()) {
        newFrameCount_++;
    }
}

void tcApp::draw() {
    clear(50);  // Dark gray background

    if (!permissionGranted_) {
        // Message when permission is not granted
        setColor(1.0f);
        drawBitmapString("Camera permission required.", 20, 30);
        drawBitmapString("Please grant camera access in System Settings.", 20, 50);
        drawBitmapString("Then restart the application.", 20, 70);
        return;
    }

    if (!grabber_.isInitialized()) {
        setColor(1.0f);
        drawBitmapString("Initializing camera...", 20, 30);
        return;
    }

    // Draw camera image (scaled to fit window)
    setColor(1.0f);
    float videoW = grabber_.getWidth();
    float videoH = grabber_.getHeight();
    float winW = getWindowWidth();
    float winH = getWindowHeight();

    // Fit while maintaining aspect ratio
    float scale = std::min(winW / videoW, winH / videoH);
    float drawW = videoW * scale;
    float drawH = videoH * scale;
    float drawX = (winW - drawW) / 2;  // Centering
    float drawY = (winH - drawH) / 2;

    grabber_.draw(drawX, drawY, drawW, drawH);

    // Display information
    int y = 20;
    setColor(colors::yellow);
    drawBitmapString("FPS: " + to_string((int)getFrameRate()), 10, y); y += 20;

    setColor(colors::cyan);
    drawBitmapString("Device [" + to_string(currentDevice_) + "]: " +
                     (currentDevice_ < devices_.size() ? devices_[currentDevice_].deviceName : "?"), 10, y); y += 20;

    setColor(colors::white);
    drawBitmapString("Size: " + to_string(grabber_.getWidth()) + "x" + to_string(grabber_.getHeight()), 10, y); y += 20;

    // Frame status
    if (grabber_.isFrameNew()) {
        setColor(colors::green);
        drawBitmapString("Frame: NEW", 10, y);
    } else {
        setColor(colors::red);
        drawBitmapString("Frame: waiting...", 10, y);
    }
    y += 20;

    setColor(colors::gray);
    drawBitmapString("New frames: " + to_string(newFrameCount_) + " / " + to_string(frameCount_), 10, y); y += 20;

    // Check pixel data (values of first few pixels)
    const unsigned char* pixels = grabber_.getPixels();
    if (pixels) {
        int sum = 0;
        for (int i = 0; i < 100 * 4; i++) {
            sum += pixels[i];
        }
        setColor(colors::magenta);
        drawBitmapString("Pixel sum (first 100): " + to_string(sum), 10, y); y += 20;
    }

    y += 10;
    setColor(colors::white);
    drawBitmapString("Press 1-" + to_string(std::min((int)devices_.size(), 9)) + " to switch camera", 10, y);
}

void tcApp::keyPressed(int key) {
    // Switch camera with number keys
    if (key >= '1' && key <= '9') {
        int deviceIdx = key - '1';
        if (deviceIdx < (int)devices_.size() && deviceIdx != currentDevice_) {
            tcLogNotice("tcApp") << "Switching to camera " << deviceIdx << ": " << devices_[deviceIdx].deviceName;
            grabber_.close();
            currentDevice_ = deviceIdx;
            grabber_.setDeviceID(currentDevice_);
            grabber_.setVerbose(true);
            if (grabber_.setup(640, 480)) {
                tcLogNotice("tcApp") << "Camera started: " << grabber_.getWidth() << "x" << grabber_.getHeight()
                              << " (" << grabber_.getDeviceName() << ")";
            } else {
                tcLogError("tcApp") << "Failed to start camera " << deviceIdx;
            }
            newFrameCount_ = 0;
            frameCount_ = 0;
        }
    }
    // Restart current camera with space
    else if (key == ' ') {
        tcLogNotice("tcApp") << "Restarting camera " << currentDevice_;
        grabber_.close();
        grabber_.setDeviceID(currentDevice_);
        grabber_.setup(640, 480);
        newFrameCount_ = 0;
        frameCount_ = 0;
    }
}
