// =============================================================================
// videoRecExample - Native video recording sample
// =============================================================================
// Renders an animated scene into an offscreen Fbo, draws it to the window, and
// records it with TrussC's native VideoRecorder (H.264 -> .mp4): AVFoundation
// on macOS, Media Foundation on Windows, GStreamer on Linux. No ffmpeg.
//
//   r : start / stop recording
//   m : switch source between the clean Fbo and the whole window (idle only)
//
// Recording uses VideoRecorder::start(): once started it auto-captures every
// frame via the afterFrame hook (the same fully-composited image get_screenshot
// returns) until close() — no per-frame addFrame() call needed.
//
// Encoder selection (Linux): the recorder picks a hardware H.264 encoder when
// one actually works (NVENC, then VA-API), and transparently falls back to
// software x264 otherwise — so this same code "just records" on any machine,
// GPU or not. The full selection order, the GStreamer plugins each backend
// needs, and troubleshooting live in this example's README.md.
//
// Output files land in bin/data (getDataPath):
//   videoRec_clean.mp4   - the Fbo only, no on-screen text
//   videoRec_screen.mp4  - the whole window, overlay included
// =============================================================================

#include "tcApp.h"
#include <cmath>
using namespace std;

void tcApp::setup() {
    fbo_.allocate(recW_, recH_);
    logNotice("tcApp") << "videoRecExample - press 'r' to record, 'm' to switch source";
}

void tcApp::update() {
}

void tcApp::renderScene() {
    fbo_.begin(0.05f, 0.06f, 0.10f, 1.0f);

    const float t = getElapsedTimef();
    const float cx = recW_ * 0.5f;
    const float cy = recH_ * 0.5f;

    // Orbiting colored beads.
    const int n = 12;
    for (int i = 0; i < n; ++i) {
        float a = TAU * i / n + t * 0.5f;
        float r = recH_ * 0.3f;
        float x = cx + cosf(a) * r;
        float y = cy + sinf(a) * r;
        setColorHSB(fmodf(i / (float)n + t * 0.1f, 1.0f), 0.7f, 1.0f);
        drawCircle(x, y, 22.0f + 10.0f * sinf(t * 2.0f + i));
    }

    // A sweeping bar makes per-frame timing obvious in the recording.
    setColor(1.0f);
    float bx = fmodf(t * 160.0f, (float)recW_);
    drawRect(bx, 0.0f, 4.0f, (float)recH_);

    fbo_.end();
}

void tcApp::draw() {
    renderScene();

    clear(0.12f);

    // Fit the Fbo into the window (letterbox).
    float s = std::min(getWindowWidth() / (float)recW_,
                       getWindowHeight() / (float)recH_);
    float w = recW_ * s;
    float h = recH_ * s;
    float x = (getWindowWidth() - w) * 0.5f;
    float y = (getWindowHeight() - h) * 0.5f;
    setColor(1.0f);
    fbo_.draw(x, y, w, h);

    // Overlay (part of the window, so it shows up in WINDOW-source recordings).
    setColor(recorder_.isRecording() ? colors::red : colors::yellow);
    string src = recordScreen_ ? "WINDOW" : "CLEAN Fbo";
    string status = recorder_.isRecording()
        ? format("● REC  {}  {} frames", src, recorder_.getFrameCount())
        : format("source: {}  (r: record, m: switch)", src);
    drawBitmapString(status, 12, 22);

    // start() handles per-frame capture automatically — nothing to do here.
}

void tcApp::toggleRecording() {
    if (recorder_.isRecording()) {
        recorder_.close();
        return;
    }
    if (recordScreen_) {
        // Match the swapchain's real pixel size (retina-aware), then auto-capture
        // the whole window each frame.
        Pixels px;
        if (grabScreen(px) && px.isAllocated()) {
            if (recorder_.setup("videoRec_screen.mp4",
                                px.getWidth(), px.getHeight(), fps_)) {
                recorder_.start();
            }
        } else {
            logError("tcApp") << "grabScreen failed; cannot start screen recording";
        }
    } else {
        // Auto-capture the clean Fbo each frame.
        if (recorder_.setup("videoRec_clean.mp4", recW_, recH_, fps_)) {
            recorder_.start(fbo_);
        }
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        toggleRecording();
    } else if ((key == 'm' || key == 'M') && !recorder_.isRecording()) {
        recordScreen_ = !recordScreen_;
    }
}

void tcApp::exit() {
    recorder_.close();
}
