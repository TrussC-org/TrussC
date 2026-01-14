#include "tcApp.h"

void tcApp::setup() {
    logNotice("TcvPlayer") << "TCV Player - Phase 1";
    logNotice("TcvPlayer") << "Drag & drop a .tcv file or press O to open";

    // Try to load sample.tcv from data folder
    string samplePath = getDataPath("sample.tcv");
    if (filesystem::exists(samplePath)) {
        loadVideo(samplePath);
    }
}

void tcApp::update() {
    if (loaded_) {
        player_.update();
    }
}

void tcApp::draw() {
    // Calculate FPS
    frameCount_++;
    float currentTime = getElapsedTimef();
    if (currentTime - lastTime_ >= 1.0f) {
        fps_ = frameCount_ / (currentTime - lastTime_);
        frameCount_ = 0;
        lastTime_ = currentTime;
    }

    clear(0.1f);

    if (!loaded_) {
        setColor(1.0f);
        drawBitmapString("TCV Player", 20, 30);
        drawBitmapString("Drag & drop a .tcv file to play", 20, 60);
        drawBitmapString("Press O to open file dialog", 20, 80);
        return;
    }

    // Draw video centered
    float videoW = player_.getWidth();
    float videoH = player_.getHeight();

    float scale = min(getWindowWidth() / videoW, (getWindowHeight() - 60) / videoH);
    float drawW = videoW * scale;
    float drawH = videoH * scale;
    float x = (getWindowWidth() - drawW) / 2;
    float y = 10;

    setColor(1.0f);
    player_.draw(x, y, drawW, drawH);

    // Draw playback info
    float infoY = y + drawH + 10;

    // FPS with fixed width (3 digits)
    char fpsStr[16];
    snprintf(fpsStr, sizeof(fpsStr), "FPS:%3d", static_cast<int>(fps_));

    setColor(1.0f);
    string info = string(fpsStr) + "  |  Frame: " + to_string(player_.getCurrentFrame()) + " / " + to_string(player_.getTotalFrames());
    info += "  |  Time: " + to_string(static_cast<int>(player_.getCurrentTime())) + "s / " + to_string(static_cast<int>(player_.getDuration())) + "s";
    info += "  |  " + string(player_.isPlaying() ? "Playing" : (player_.isPaused() ? "Paused" : "Stopped"));
    drawBitmapString(info, 20, infoY);

    drawBitmapString("SPACE: Play/Pause  |  LEFT/RIGHT: Prev/Next frame  |  R: Restart", 20, infoY + 15);
}

void tcApp::keyPressed(int key) {
    if (key == 'o' || key == 'O') {
        auto result = loadDialog("Select .tcv file");
        if (result.success && !result.filePath.empty()) {
            loadVideo(result.filePath);
        }
    }
    else if (key == ' ') {
        if (loaded_) {
            if (player_.isPlaying()) {
                player_.togglePause();
            } else {
                player_.play();
            }
        }
    }
    else if (key == KEY_LEFT) {
        if (loaded_) {
            player_.previousFrame();
        }
    }
    else if (key == KEY_RIGHT) {
        if (loaded_) {
            player_.nextFrame();
        }
    }
    else if (key == 'r' || key == 'R') {
        if (loaded_) {
            player_.firstFrame();
            player_.play();
        }
    }
}

void tcApp::filesDropped(const vector<string>& files) {
    if (!files.empty()) {
        loadVideo(files[0]);
    }
}

void tcApp::loadVideo(const string& path) {
    if (player_.load(path)) {
        loaded_ = true;
        player_.play();
        logNotice("TcvPlayer") << "Loaded: " << path;
    } else {
        loaded_ = false;
        logError("TcvPlayer") << "Failed to load: " << path;
    }
}
