#include "tcApp.h"

void tcApp::setup() {
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    addChild(replay);
    replay->startRecording(inputreplay::RecordingMode::Ring, chrono::seconds(30));
}
void tcApp::keyPressed(int key) {
    if (key != 'r' && key != 'R') return;
    // Replay reproduces only from the state recording started in; drags always start from home, so reset before Play.
    ball = home;
    dragging = false;
    status.clear();
}
void tcApp::mousePressed(const MouseEventArgs& e) {
    // Only a ball at home can be grabbed, so every drag starts from home.
    dragging = ball == home && !replay->isPointerOver() && (e.pos - home).length() < radius;
}
void tcApp::mouseReleased(const MouseEventArgs& e) {
    if (dragging) ball = home;
    dragging = false;
}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {
    if (!dragging) return;
    ball = e.pos;
    float wall = getWidth() / 2;
    // Bug: only checks where the ball lands, not the path it took.
    if (ball.x >= wall - radius && ball.x <= wall + wallWidth + radius) {
        ball = home;
        dragging = false;
    }
    if (ball.x <= wall + wallWidth + radius) return;
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
    drawRect(getWidth() / 2 + wallWidth, 0, getWidth() / 2 - wallWidth, getHeight());
    setColor(0.80f, 0.82f, 0.85f);
    drawRect(getWidth() / 2, 0, wallWidth, getHeight());
    setColor(1.0f, 0.62f, 0.38f);
    drawCircle(ball, radius);
    setColor(1.0f);
    drawBitmapString("Drag the ball  |  R: reset  |  F9: toolbar", 20, 70);
    drawBitmapString("FORBIDDEN AREA", getWidth() / 2 + 30, 110);
    if (status == "caught it") drawBitmapString(status + "\n" + savedPath + "\nR, then Open -> Play to reproduce", 20, 490);
    else drawBitmapString(status, 20, 490);
}
