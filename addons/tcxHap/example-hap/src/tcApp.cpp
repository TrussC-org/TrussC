#include "tcApp.h"

void tcApp::setup() {
    logNotice("tcApp") << "=== HAP Player Example ===";
    logNotice("tcApp") << "Drop a HAP-encoded .mov file to play";
    logNotice("tcApp") << "Keys: Space=Play/Pause, R=Restart, L=Loop toggle";

    // MCP tool for loading video files
    mcp::tool("load_file", "Load a HAP video file")
        .arg<string>("path", "Path to HAP-encoded .mov file")
        .bind([this](const json& args) -> json {
            string path = args.at("path").get<string>();
            logNotice("tcApp") << "MCP: Loading " << path;
            bool success = player_.load(path);
            if (success) {
                player_.play();
                statusText_ = "Loaded: " + path;
                return json{
                    {"status", "ok"},
                    {"width", player_.getWidth()},
                    {"height", player_.getHeight()},
                    {"frames", player_.getTotalFrames()},
                    {"duration", player_.getDuration()}
                };
            } else {
                statusText_ = "Failed to load (not HAP?): " + path;
                return json{{"status", "error"}, {"message", "Failed to load: " + path}};
            }
        });
}

void tcApp::update() {
    player_.update();
}

void tcApp::draw() {
    clear(0.1f);

    if (player_.isLoaded()) {
        // Draw video centered
        float vw = player_.getWidth();
        float vh = player_.getHeight();
        float scale = min(getWindowWidth() / vw, (getWindowHeight() - 80) / vh);
        float x = (getWindowWidth() - vw * scale) / 2;
        float y = (getWindowHeight() - 80 - vh * scale) / 2;

        player_.draw(x, y, vw * scale, vh * scale);

        // Info bar
        setColor(0.8f, 0.8f, 0.85f);
        string info = format("{}x{} | Frame {}/{} | {:.1f}s / {:.1f}s | {}",
            (int)vw, (int)vh,
            player_.getCurrentFrame(), player_.getTotalFrames(),
            player_.getCurrentTime(), player_.getDuration(),
            player_.isPlaying() ? "Playing" : (player_.isPaused() ? "Paused" : "Stopped"));
        drawBitmapString(info, 20, getWindowHeight() - 50);

        string controls = "Space: Play/Pause | R: Restart | L: Loop | 0-9: Seek | Left/Right: Frame step";
        setColor(0.5f, 0.5f, 0.55f);
        drawBitmapString(controls, 20, getWindowHeight() - 30);
    } else {
        // No video loaded
        setColor(0.6f, 0.6f, 0.65f);
        drawBitmapString(statusText_, 20, getWindowHeight() / 2);
    }
}

void tcApp::keyPressed(int key) {
    if (!player_.isLoaded()) return;

    switch (key) {
        case ' ':
            if (player_.isPlaying()) {
                player_.setPaused(!player_.isPaused());
            } else {
                player_.play();
            }
            break;
        case 'r':
        case 'R':
            player_.stop();
            player_.play();
            break;
        case 'l':
        case 'L':
            player_.setLoop(!player_.isLoop());
            logNotice("tcApp") << "Loop: " << (player_.isLoop() ? "ON" : "OFF");
            break;
        case SAPP_KEYCODE_LEFT:
            player_.previousFrame();
            break;
        case SAPP_KEYCODE_RIGHT:
            player_.nextFrame();
            break;
        // Number keys 1-9 for seeking to 10%-90%
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
            player_.setPosition((key - '0') * 0.1f);
            logNotice("tcApp") << "Seek to " << ((key - '0') * 10) << "%";
            break;
        case '0':
            player_.setPosition(0);
            logNotice("tcApp") << "Seek to 0%";
            break;
    }
}

void tcApp::filesDropped(const vector<string>& files) {
    if (files.empty()) return;

    const string& path = files[0];
    logNotice("tcApp") << "Loading: " << path;

    if (player_.load(path)) {
        player_.play();
        statusText_ = "Loaded: " + path;
    } else {
        statusText_ = "Failed to load (not HAP?): " + path;
    }
}
