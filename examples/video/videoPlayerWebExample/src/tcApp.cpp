// =============================================================================
// videoPlayerWebExample - TrussC VideoPlayer sample for Web
// =============================================================================
// Web-only VideoPlayer sample using external URL.
// - Space: Play/Pause
// - R: Restart from beginning
// - Left/Right arrows: Seek
// - Up/Down arrows: Volume
//
// Video: "Big Buck Bunny"
// (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org
// Licensed under Creative Commons Attribution 3.0
// https://creativecommons.org/licenses/by/3.0/
// https://peach.blender.org/
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Video Player Example (Web)");

    // Big Buck Bunny (CC BY 3.0)
    string videoUrl = "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";

    tcLogNotice("tcApp") << "Loading video from URL...";

    if (video_.load(videoUrl)) {
        tcLogNotice("tcApp") << "Video loading started";
        // Start paused, user presses Space to play
        loading_ = false;
    } else {
        tcLogError("tcApp") << "Failed to load video";
    }
}

void tcApp::update() {
    video_.update();

    // Detect when loading completes
    if (loading_ && video_.isLoaded()) {
        loading_ = false;
        tcLogNotice("tcApp") << "Video loaded: " << (int)video_.getWidth() << "x"
                             << (int)video_.getHeight() << ", "
                             << video_.getDuration() << " sec";
    }
}

void tcApp::draw() {
    clear(30);

    if (video_.isLoaded()) {
        // Draw video centered
        float scale = std::min(getWindowWidth() / video_.getWidth(),
                               getWindowHeight() / video_.getHeight());
        float w = video_.getWidth() * scale;
        float h = video_.getHeight() * scale;
        float x = (getWindowWidth() - w) / 2;
        float y = (getWindowHeight() - h) / 2;

        video_.draw(x, y, w, h);

        // Progress bar
        float barY = getWindowHeight() - 30;
        float barHeight = 10;
        float progress = video_.getPosition();

        // Background
        setColor(50);
        drawRect(20, barY, getWindowWidth() - 40, barHeight);

        // Progress
        setColor(100, 200, 100);
        drawRect(20, barY, (getWindowWidth() - 40) * progress, barHeight);

        // Info display
        if (showInfo_) {
            setColor(255);
            int currentFrame = video_.getCurrentFrame();
            int totalFrames = video_.getTotalFrames();
            float currentTime = video_.getPosition() * video_.getDuration();

            string info = formatTime(currentTime) + " / " +
                         formatTime(video_.getDuration()) +
                         " (" + to_string(currentFrame) + "/" +
                         to_string(totalFrames) + ")";

            drawBitmapString(info, 20, 20);

            string state = video_.isPlaying() ? "Playing" :
                          video_.isPaused() ? "Paused" : "Stopped";
            drawBitmapString("State: " + state, 20, 40);
            drawBitmapString("Volume: " + to_string((int)(video_.getVolume() * 100)) + "%", 20, 60);
        }
    } else {
        // Loading / Ready to play
        setColor(255);
        drawBitmapString("Press Space to play", getWindowWidth() / 2 - 60, getWindowHeight() / 2 - 20);
        drawBitmapString("Big Buck Bunny (CC BY 3.0)", getWindowWidth() / 2 - 80, getWindowHeight() / 2);
    }

    // Controls help
    setColor(200);
    drawBitmapString("Space: Play/Pause | R: Restart | Arrows: Seek/Volume | I: Info",
                    20, getWindowHeight() - 50);
}

string tcApp::formatTime(float seconds) {
    int min = (int)seconds / 60;
    int sec = (int)seconds % 60;
    return to_string(min) + ":" + (sec < 10 ? "0" : "") + to_string(sec);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        // Toggle play/pause
        if (video_.isPlaying()) {
            video_.setPaused(true);
        } else {
            video_.setPaused(false);
            if (!video_.isPlaying()) {
                video_.play();
            }
        }
    }
    else if (key == 'r' || key == 'R') {
        video_.stop();
        video_.play();
    }
    else if (key == KEY_LEFT) {
        // Seek backward 5%
        video_.setPosition(video_.getPosition() - 0.05f);
    }
    else if (key == KEY_RIGHT) {
        // Seek forward 5%
        video_.setPosition(video_.getPosition() + 0.05f);
    }
    else if (key == KEY_UP) {
        // Volume up
        video_.setVolume(video_.getVolume() + 0.1f);
    }
    else if (key == KEY_DOWN) {
        // Volume down
        video_.setVolume(video_.getVolume() - 0.1f);
    }
    else if (key == 'i' || key == 'I') {
        showInfo_ = !showInfo_;
    }
}
