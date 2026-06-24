// =============================================================================
// videoRecAdvancedExample - the full recording toolbox
// =============================================================================
// Shows every way to record, and makes the screen/Fbo distinction visible:
//
//   GREEN box + "drawn in fbo"   — rendered INTO an Fbo (the clean output layer)
//   WHITE box + "drawn in draw()" — rendered straight to the window in draw()
//
// So a recording's contents tell you exactly which surface it captured:
//   r : ScreenRecorder, whole WINDOW  -> both boxes  (advanced_window.mp4)
//   f : ScreenRecorder, the Fbo only  -> GREEN only  (advanced_fbo.mp4)
//   e : VideoWriter, offline export of the Fbo (deterministic, exact 5s)
//                                      -> GREEN only  (advanced_offline.mp4)
//
// ScreenRecorder = live, auto-captures every frame (window or fbo) at wall-clock
// speed until stop(). VideoWriter = low-level, you open() and feed addFrame()
// yourself for a fixed-rate, deterministic file. Both are native (no ffmpeg) and
// take a VideoRecordSettings { codec, fps, bitrate, ... } — e.g. add
// { .codec = VideoCodec::ProRes422 } (macOS) or { .codec = VideoCodec::HEVC }.
// =============================================================================

#include "tcApp.h"
using namespace std;

void tcApp::setup() {
    fbo_.allocate(getWidth(), getHeight());
    logNotice("tcApp") << "videoRecAdvancedExample - r: window  f: fbo  e: offline export";
}

void tcApp::update() {
}

void tcApp::drawSpinBox(float cx, float cy, float size) {
    pushMatrix();
    translate(cx, cy);
    rotate(getElapsedTimef() * 0.5f, getElapsedTimef() * 0.35f, 0);
    noFill();
    drawBox(size);
    popMatrix();
}

void tcApp::draw() {
    // --- The clean layer: GREEN, drawn INSIDE the Fbo --------------------------
    fbo_.begin(0.08f, 0.10f, 0.12f, 1.0f);
        setColor(0.30f, 1.0f, 0.45f);   // green
        drawSpinBox(fbo_.getWidth() * 0.33f, fbo_.getHeight() * 0.5f, 150.0f);
        drawBitmapString("drawn in fbo",
                         fbo_.getWidth() * 0.33f - 44, fbo_.getHeight() * 0.5f + 120);
    fbo_.end();

    // --- The window: blit the Fbo, then draw the WHITE layer in draw() ---------
    clear(0.04f);
    setColor(1.0f);
    fbo_.draw(0, 0, getWidth(), getHeight());

    setColor(1.0f);                      // white
    drawSpinBox(getWidth() * 0.67f, getHeight() * 0.5f, 150.0f);
    drawBitmapString("drawn in draw()",
                     getWidth() * 0.67f - 52, getHeight() * 0.5f + 120);

    setColor(colors::yellow);
    drawBitmapString(statusLine(), 12, 22);

    // VideoWriter offline export: feed one Fbo frame per draw, then finalize.
    if (exporting_) {
        writer_.addFrame(fbo_);
        if (++exportFrame_ >= exportFrames_) {
            writer_.close();
            exporting_ = false;
        }
    }
}

void tcApp::startExport() {
    // Low-level: open with explicit size; settings carry codec/fps/bitrate.
    if (writer_.open("advanced_offline.mp4", fbo_.getWidth(), fbo_.getHeight(),
                     { .fps = fps_ })) {
        exporting_ = true;
        exportFrame_ = 0;
    }
}

string tcApp::statusLine() {
    if (exporting_)
        return format("offline export (VideoWriter)  {}/{} frames", exportFrame_, exportFrames_);
    if (rec_.isRecording())
        return format("REC {} frames  (key: stop)", rec_.getFrameCount());
    return "r: record window (both)   f: record fbo (green only)   e: offline export";
}

void tcApp::keyPressed(int key) {
    if (exporting_) return;  // busy

    if (key == 'R') {
        // ScreenRecorder instance, source = whole window (white + green).
        if (rec_.isRecording()) rec_.stop();
        else                    rec_.start("advanced_window.mp4", { .fps = fps_ });
    } else if (key == 'F') {
        // ScreenRecorder instance, source = the Fbo (green only, GUI-free).
        if (rec_.isRecording()) rec_.stop();
        else                    rec_.start(fbo_, "advanced_fbo.mp4", { .fps = fps_ });
    } else if (key == 'E') {
        // VideoWriter, offline deterministic export of the Fbo.
        if (!rec_.isRecording()) startExport();
    }
}

void tcApp::exit() {
    rec_.stop();
    writer_.close();
}
