#include "tcApp.h"

void tcApp::setup() {
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    addChild(replay);
    replay->startRecording(inputreplay::RecordingMode::Ring, chrono::seconds(30));
}
void tcApp::keyPressed(int key) {
    if (key != 'r' && key != 'R') return;
    // Replay reproduces only from the state recording started in; the ball is home at every press, so reset before Play.
    ball = home;
    dragging = false;
    status.clear();
}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {
    if (!dragging) return;
    ball = e.pos;
    float wall = getWidth() / 2;
    // Bug: only checks where the ball lands, not the path it took.
    if (ball.x >= wall - 18 && ball.x <= wall + 24) {
        ball = home;
        dragging = false;
    }
    if (ball.x <= wall + 24) return;
    dragging = false;
    status = e.injected ? "reproduced it" : "caught it";
    if (e.injected) return;
    auto path = getDataPath("ring-" + getTimestampString("%Y%m%d-%H%M%S-%i") + ".jsonl");
    bool saved = replay->flush(path);
    replay->startRecording(inputreplay::RecordingMode::Ring, chrono::seconds(30));
    savedPath = "data/" + path.filename().string();
    if (saved) logNotice("ringRecording") << "Wrote " << path;
    else {
        status = "Could not save recording";
        logWarning("ringRecording") << "Could not write " << path;
    }
}
void tcApp::draw() {
    clear(0.07f, 0.08f, 0.10f);
    setColor(0.24f, 0.11f, 0.13f);
    drawRect(getWidth() / 2 + 6, 0, getWidth() / 2 - 6, getHeight());
    setColor(0.80f, 0.82f, 0.85f);
    drawRect(getWidth() / 2, 0, 6, getHeight());
    setColor(1.0f, 0.62f, 0.38f);
    drawCircle(ball, 18);
    setColor(1.0f);
    drawBitmapString("Drag the ball  |  R: reset  |  F9: toolbar", 20, 70);
    drawBitmapString("FORBIDDEN AREA", getWidth() / 2 + 30, 110);
    if (status == "caught it") drawBitmapString(status + "\n" + savedPath + "\nR, then Open -> Play to reproduce", 20, 490);
    else drawBitmapString(status, 20, 490);
}
