#include "tcApp.h"

void tcApp::setup() {
    logNotice("TcvPlayer") << "TCV Player - Phase 1";
    logNotice("TcvPlayer") << "Drag & drop a .tcv file or press O to open";

    // Check command line arguments first
    int argc = getArgCount();
    char** argv = getArgValues();
    if (argc > 1) {
        loadVideo(argv[1]);
        return;
    }

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

    // Draw debug overlay if enabled
    if (player_.isDebug()) {
        player_.drawDebugOverlay(x, y, scale);
    }

    // Draw playback info
    float infoY = y + drawH + 10;

    // FPS with fixed width (3 digits)
    char fpsStr[16];
    snprintf(fpsStr, sizeof(fpsStr), "FPS:%3d", static_cast<int>(fps_));

    setColor(1.0f);
    string info = string(fpsStr) + "  |  Frame: " + to_string(player_.getCurrentFrame()) + " / " + to_string(player_.getTotalFrames());
    info += "  |  Time: " + to_string(static_cast<int>(player_.getCurrentTime())) + "s / " + to_string(static_cast<int>(player_.getDuration())) + "s";
    info += "  |  " + string(player_.isPlaying() ? "Playing" : (player_.isPaused() ? "Paused" : "Stopped"));

    // Show average decode time
    char decodeStr[32];
    snprintf(decodeStr, sizeof(decodeStr), "  |  Decode: %.2fms", player_.getAvgDecodeTimeMs());
    info += decodeStr;

    drawBitmapString(info, 20, infoY);

    string helpText = "SPACE: Play/Pause  |  LEFT/RIGHT: Prev/Next frame  |  R: Restart  |  D: Debug";
    if (player_.isDebug()) {
        helpText += " [ON - Green:Solid, Yellow:Q-BC7, Red:BC7]";
    }
    drawBitmapString(helpText, 20, infoY + 15);

    // Draw seekbar
    float seekbarY = infoY + 35;
    float seekbarH = 24;
    float seekbarMargin = 20;
    seekbarRect_ = Rect(seekbarMargin, seekbarY, getWindowWidth() - seekbarMargin * 2, seekbarH);

    // Background
    setColor(0.25f);
    drawRect(seekbarRect_.x, seekbarRect_.y, seekbarRect_.width, seekbarRect_.height);

    // Progress
    float progress = static_cast<float>(player_.getCurrentFrame()) / max(1, player_.getTotalFrames() - 1);
    setColor(0.4f, 0.6f, 0.9f);
    drawRect(seekbarRect_.x, seekbarRect_.y, seekbarRect_.width * progress, seekbarRect_.height);

    // Handle
    float handleX = seekbarRect_.x + seekbarRect_.width * progress;
    float handleW = 12;
    float handleH = seekbarH + 6;
    setColor(1.0f);
    drawRect(handleX - handleW / 2, seekbarRect_.y - 3, handleW, handleH);
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
    else if (key == 'd' || key == 'D') {
        if (loaded_) {
            player_.setDebug(!player_.isDebug());
            logNotice("TcvPlayer") << "Debug mode: " << (player_.isDebug() ? "ON" : "OFF");
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

void tcApp::mousePressed(Vec2 pos, int button) {
    if (!loaded_ || button != 0) return;

    if (isInsideSeekbar(pos.x, pos.y)) {
        isDraggingSeekbar_ = true;
        wasPlayingBeforeDrag_ = player_.isPlaying();
        if (wasPlayingBeforeDrag_) {
            player_.togglePause();
        }
        updateSeekbarFromMouse(pos.x);
    }
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    if (!loaded_ || button != 0) return;

    if (isDraggingSeekbar_) {
        updateSeekbarFromMouse(pos.x);
    }
}

void tcApp::mouseReleased(Vec2 pos, int button) {
    if (!loaded_ || button != 0) return;

    if (isDraggingSeekbar_) {
        isDraggingSeekbar_ = false;
        if (wasPlayingBeforeDrag_) {
            player_.play();
        }
    }
}

bool tcApp::isInsideSeekbar(float x, float y) {
    // Expand hit area vertically for easier clicking
    float expandedY = seekbarRect_.y - 5;
    float expandedH = seekbarRect_.height + 10;
    return x >= seekbarRect_.x && x <= seekbarRect_.x + seekbarRect_.width &&
           y >= expandedY && y <= expandedY + expandedH;
}

void tcApp::updateSeekbarFromMouse(float x) {
    float relX = (x - seekbarRect_.x) / seekbarRect_.width;
    relX = max(0.0f, min(1.0f, relX));
    int frame = static_cast<int>(relX * (player_.getTotalFrames() - 1));
    player_.setFrame(frame);
}
