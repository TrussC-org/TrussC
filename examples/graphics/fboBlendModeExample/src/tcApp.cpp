#include "tcApp.h"

static constexpr int kPane = 400;   // one test pane (and the Fbo) is 400x400

void tcApp::setup() {
    logNotice("tcApp") << "fboBlendModeExample: setBlendMode inside an Fbo pass";
    fbo_.allocate(kPane, kPane);
}

// Two half-gray rects; the second is drawn with BlendMode::Add, so where they
// overlap the result reads 0.5 + 0.5 = 1.0 (white). A bitmap-string draw sits
// between the two: it swaps in its own alpha pipeline temporarily, so the
// second rect also covers the restore path bringing the Add pipeline back
// (not just setBlendMode itself).
void tcApp::drawScene() {
    setColor(0.5f, 0.5f, 0.5f);
    drawRect(40, 40, 220, 220);

    setBlendMode(BlendMode::Add);
    drawBitmapString("blend: Add", 40, 380);
    drawRect(140, 140, 220, 220);
    resetBlendMode();
}

void tcApp::draw() {
    clear(0, 0, 0);

    // Left: straight to the swapchain (the reference rendering)
    drawScene();

    // Right: the identical scene inside an Fbo pass
    fbo_.begin(0.0f, 0.0f, 0.0f, 1.0f);
    drawScene();
    fbo_.end();
    setColor(1.0f);
    fbo_.draw(480, 0);

    setColor(1.0f);
    drawBitmapString("swapchain: overlap is WHITE (0.5+0.5)", 40, 490);
    drawBitmapString("fbo: same scene - overlap is WHITE too", 520, 490);
}
