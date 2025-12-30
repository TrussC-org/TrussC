// =============================================================================
// videoPlayerExample - TrussC VideoPlayer sample
// =============================================================================
// Demonstrates video playback using tc::VideoPlayer.
// - Space: Play/Pause
// - R: Restart from beginning
// - Left/Right arrows: Seek
// - Up/Down arrows: Volume
// - L: Load video (enter path in console)
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Video Player Example");

    // You can set a video path here for testing
    // videoPath_ = "/path/to/your/video.mp4";
    // loadVideo(videoPath_);

    logNotice("tcApp") << "Press 'L' to load a video file";
}

void tcApp::loadVideo(const string& path) {
    logNotice("tcApp") << "Loading video: " << path;

    if (video_.load(path)) {
        logNotice("tcApp") << "Video loaded: " << (int)video_.getWidth() << "x"
                             << (int)video_.getHeight() << ", "
                             << video_.getDuration() << " sec";
        video_.play();
    } else {
        logError("tcApp") << "Failed to load video: " << path;
    }
}

void tcApp::update() {
    video_.update();
}

void tcApp::draw() {
    clear(0.12f);

    if (video_.isLoaded()) {
        // Draw video centered
        float scale = std::min(getWindowWidth() / video_.getWidth(),
                              getWindowHeight() / video_.getHeight());
        float w = video_.getWidth() * scale;
        float h = video_.getHeight() * scale;
        float x = (getWindowWidth() - w) / 2;
        float y = (getWindowHeight() - h) / 2;

        video_.draw(x, y, w, h);

        // Progress bar at bottom
        float barHeight = 10;
        float barY = getWindowHeight() - barHeight;
        float progress = video_.getPosition();

        // Background
        setColor(0.2f);
        drawRect(20, barY, getWindowWidth() - 40, barHeight);

        // Progress
        setColor(0.4f, 0.78f, 0.4f);
        drawRect(20, barY, (getWindowWidth() - 40) * progress, barHeight);

        // Info display at top
        if (showInfo_) {
            pushStyle();
            setTextAlign(Left, Baseline);
            setColor(1.0f);
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
            setTextAlign(Center, Baseline);
            drawBitmapString("State: " + state, getWindowWidth() / 2, 20);

            setTextAlign(Right, Baseline);
            drawBitmapString("Volume: " + to_string((int)(video_.getVolume() * 100)) + "%", getWindowWidth() - 20, 20);
            popStyle();
        }
    } else {
        // No video loaded
        pushStyle();
        setColor(1.0f);
        setTextAlign(Center, Baseline);
        drawBitmapString("No video loaded", getWindowWidth() / 2, getWindowHeight() / 2 - 20);
        drawBitmapString("Press 'L' or drop a video file", getWindowWidth() / 2, getWindowHeight() / 2);
        popStyle();
    }

    // Controls help
    setColor(0.78f);
    drawBitmapString("Space: Play/Pause | R: Restart | Arrows: Seek/Volume | I: Info | L: Load",
                    20, getWindowHeight() - 30);
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
    else if (key == 'l' || key == 'L') {
        // Open file dialog
        auto result = loadDialog("Select Video File");
        if (result.success) {
            loadVideo(result.filePath);
        }
    }
}

void tcApp::filesDropped(const vector<string>& files) {
    if (!files.empty()) {
        loadVideo(files[0]);
    }
}
