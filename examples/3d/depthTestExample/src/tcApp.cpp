#include "tcApp.h"

void tcApp::update() {
    time += getDeltaTime();
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);

    float cx = getWidth() * 0.5f;
    float cy = getHeight() * 0.5f;

    // --- 1. Depth-tested 3D (default screen pipeline) ---
    // The near box is drawn FIRST; depth testing (not draw order) is what
    // makes it occlude the middle box where they overlap.
    setColor(0.9f, 0.2f, 0.2f);  // red, near (z = +120)
    pushMatrix();
    translate(cx - 60, cy - 30, 120);
    rotateY(time * 0.4f);
    rotateX(TAU / 24);
    drawBox(150);
    popMatrix();

    setColor(0.2f, 0.8f, 0.3f);  // green, middle (z = 0), drawn second
    pushMatrix();
    translate(cx + 60, cy + 10, 0);
    rotateY(-time * 0.3f);
    rotateX(TAU / 30);
    drawBox(150);
    popMatrix();

    // --- 2. Additive glow (blend pipelines have NO depth, by design) ---
    setBlendMode(BlendMode::Add);
    setColor(0.3f, 0.25f, 0.05f);
    drawCircle(cx, cy - 120, 90 + sin(time * 1.5f) * 12);

    // --- 3. Restore depth mid-scene with enableDepthTest() ---
    // The blue box is drawn LAST but sits FARTHEST (z = -150). With depth
    // testing restored it is correctly hidden behind the nearer boxes.
    // Comment out enableDepthTest() to see it wrongly cover everything.
    resetBlendMode();
    enableDepthTest();
    setColor(0.25f, 0.4f, 0.95f);
    pushMatrix();
    translate(cx + 10, cy - 10, -150);
    rotateY(time * 0.25f);
    rotateX(-TAU / 28);
    drawBox(210);
    popMatrix();

    // --- 4. Back to default (no depth) for the 2D overlay ---
    disableDepthTest();
    setColor(0, 0, 0, 0.65f);
    drawRect(20, 20, 360, 60);
    setColor(1, 1, 1);
    drawBitmapString("enableDepthTest / disableDepthTest", 32, 40);
    drawBitmapString(string("depthTest is ") + (isDepthTestEnabled() ? "ON" : "OFF")
                     + " here  (overlay draws on top)", 32, 58);

    // =========================================================
    // Challenge:
    //   1. Comment out enableDepthTest() in step 3 — the far blue box now
    //      covers the near ones (draw order wins without depth testing)
    //   2. Move the glow circle after step 3 and watch how blend modes
    //      interact with the restored depth state
    //   3. Swap BlendMode::Add for Screen or Multiply in step 2
    // =========================================================
}
