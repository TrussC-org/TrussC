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

    tcLogNotice("tcApp") << "Press 'L' to load a video file";
}

void tcApp::loadVideo(const string& path) {
    tcLogNotice("tcApp") << "Loading video: " << path;

    if (video_.load(path)) {
        tcLogNotice("tcApp") << "Video loaded: " << (int)video_.getWidth() << "x"
                             << (int)video_.getHeight() << ", "
                             << video_.getDuration() << " sec";
        video_.play();
    } else {
        tcLogError("tcApp") << "Failed to load video: " << path;
    }
}

void tcApp::update() {
    video_.update();
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

        // Draw progress bar
        float barY = getWindowHeight() - 30;
        float barHeight = 10;
        float progress = video_.getPosition();

        // Background
        setColor(50);
        drawRect(20, barY, getWindowWidth() - 40, barHeight);

        // Progress
        setColor(100, 200, 100);
        drawRect(20, barY, (getWindowWidth() - 40) * progress, barHeight);

        // Info
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
        // No video loaded
        setColor(255);
        drawBitmapString("No video loaded", getWindowWidth() / 2 - 50, getWindowHeight() / 2 - 20);
        drawBitmapString("Press 'L' or drop a video file", getWindowWidth() / 2 - 90, getWindowHeight() / 2);
    }

    // Controls help
    setColor(200);
    drawBitmapString("Space: Play/Pause | R: Restart | Arrows: Seek/Volume | I: Info | L: Load",
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
