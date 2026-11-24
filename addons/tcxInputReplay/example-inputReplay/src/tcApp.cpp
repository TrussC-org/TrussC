#include "tcApp.h"

// How long (seconds) the trail and the click marks stay visible.
static const float TRAIL_LIFE = 1.5f;
static const float CLICK_LIFE = 2.0f;

void tcApp::setup() {
    // F9 shows and hides the toolbar. With no argument, no key is assigned.
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    // Optional: start and stop recording from the keyboard (no key by default).
    // replay->setRecordKey(KEY_F8);
    addChild(replay);
}

void tcApp::update() {
    // Drop marks that have fully faded out.
    float now = getElapsedTimef();
    while (!trail.empty() && now - trail.front().time > TRAIL_LIFE) trail.pop_front();
    while (!clicks.empty() && now - clicks.front().time > CLICK_LIFE) clicks.pop_front();
}

void tcApp::mouseMoved(Vec2 pos) {
    trail.push_back({pos, getElapsedTimef()});
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    trail.push_back({pos, getElapsedTimef()});
}

void tcApp::mousePressed(Vec2 pos, int button) {
    if (replay->isPointerOver()) return;   // the click is for the toolbar
    clicks.push_back({pos, getElapsedTimef()});
}

void tcApp::draw() {
    clear(0.07f, 0.08f, 0.10f);
    float now = getElapsedTimef();

    // Trail: newer points are brighter, older points fade away.
    for (size_t i = 0; i < trail.size(); i++) {
        float a = 1.0f - (now - trail[i].time) / TRAIL_LIFE;
        setColor(0.45f, 0.80f, 1.0f, a * 0.9f);
        drawCircle(trail[i].pos, 1.5f);
        if (i > 0) drawLine(trail[i - 1].pos, trail[i].pos);
    }

    // Clicks: a solid dot that fades out.
    for (auto& c : clicks) {
        float a = 1.0f - (now - c.time) / CLICK_LIFE;
        setColor(1.0f, 0.62f, 0.38f, a);
        drawCircle(c.pos, 10);
    }
}
