#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);
    // The size we load the font at sets the em scale of the path output.
    // Path output stays crisp at any draw-time scale, so we pick a moderate
    // size and rely on transform scaling for the big strokes below.
    fontDemo.load(TC_FONT_SERIF_JA, 64);
    fontV.load(TC_FONT_SERIF_JA, 48);
    fontV.setWritingMode(WritingMode::VerticalRL);
    fontLabel.load(TC_FONT_SANS, 13);
}

void tcApp::update() {
    spin_ += (float)getDeltaTime() * 0.4f;
}

void tcApp::draw() {
    clear(0.06f, 0.07f, 0.09f);
    const float W = getWidth();
    const float H = getHeight();

    setColor(0.85f);
    fontLabel.drawString("Vector glyph paths — Font::getStringPath", 24, 22, Left, Top);
    setColor(0.55f);
    fontLabel.drawString(
        "One Path per string, subpath per contour. drawStroke / drawFill / "
        "transform freely; same layout pipeline as drawString.",
        24, 42, Left, Top);

    // =========================================================================
    // Panel 1 (top-left): static stroke at large scale — atlas comparison
    // =========================================================================
    const float p1x = 24, p1y = 78, p1w = W * 0.55f - 36, p1h = 260;
    setColor(0.12f, 0.14f, 0.18f);
    drawRect(p1x, p1y, p1w, p1h);

    // Atlas (drawString) version — same string, scaled by sokol_gl transform.
    pushMatrix();
    translate(p1x + 20, p1y + 120);
    scale(2.4f, 2.4f);
    setColor(0.55f, 0.45f, 0.35f);
    fontDemo.drawString("TrussC", 0, 0, Left, Baseline);
    popMatrix();

    // Path version — stroked outline.
    pushMatrix();
    translate(p1x + 20, p1y + 230);
    scale(2.4f, 2.4f);
    Path p1Path = fontDemo.getStringPath("TrussC", 0, 0, Left, Baseline);
    setColor(0.85f, 0.92f, 1.0f);
    setStrokeWeight(1.2f);
    p1Path.drawStroke();
    popMatrix();

    setColor(0.5f);
    fontLabel.drawString("Atlas drawString (top) vs Path drawStroke (bottom) @ 2.4x",
                         p1x + 12, p1y + p1h - 22, Left, Top);

    // =========================================================================
    // Panel 2 (top-right): animated rotate + pulse scale (stroke)
    // =========================================================================
    const float p2x = p1x + p1w + 16, p2y = p1y, p2w = W - p2x - 24, p2h = p1h;
    setColor(0.12f, 0.14f, 0.18f);
    drawRect(p2x, p2y, p2w, p2h);

    const float cx = p2x + p2w / 2.f;
    const float cy = p2y + p2h / 2.f;
    const float pulseScale = 1.6f + 0.6f * std::sin((float)getElapsedTime() * 1.5f);

    pushMatrix();
    translate(cx, cy);
    rotate(spin_);
    scale(pulseScale, pulseScale);

    Path spinPath = fontDemo.getStringPath("TRUSSC", 0, 0, Center, Center);
    setColor(0.95f, 0.75f, 0.40f);
    setStrokeWeight(1.0f);
    spinPath.drawStroke();

    popMatrix();

    setColor(0.5f);
    fontLabel.drawString("rotate(t) + scale(1.0..2.2)", p2x + 12, p2y + p2h - 22, Left, Top);

    // =========================================================================
    // Panel 3 (bottom-left): vertical writing via getStringPath (stroke)
    // =========================================================================
    const float p3x = 24, p3y = p1y + p1h + 16, p3w = p1w, p3h = H - p3y - 24;
    setColor(0.12f, 0.14f, 0.18f);
    drawRect(p3x, p3y, p3w, p3h);

    pushMatrix();
    translate(p3x + p3w - 40, p3y + 24);
    scale(0.85f, 0.85f);
    Path vPath = fontV.getStringPath("猫も杓子もTrussC", 0, 0, Right, Top);
    setColor(0.70f, 0.95f, 0.80f);
    setStrokeWeight(1.0f);
    vPath.drawStroke();
    popMatrix();

    setColor(0.5f);
    fontLabel.drawString("Vertical writing — forEachGlyph routes path emit the same way",
                         p3x + 12, p3y + p3h - 22, Left, Top);

    // =========================================================================
    // Panel 4 (bottom-right): per-glyph wiggle with drawFill (e / a holes
    // get punched automatically, no special code needed).
    // =========================================================================
    const float p4x = p2x, p4y = p3y, p4w = p2w, p4h = p3h;
    setColor(0.12f, 0.14f, 0.18f);
    drawRect(p4x, p4y, p4w, p4h);

    const string wiggleText = "Wave";
    const float glyphScale = 2.0f;
    const float wt = (float)getElapsedTime();
    const float wcy = p4y + p4h / 2.f;
    const float wcx = p4x + p4w / 2.f;
    const float em = 64.f;  // matches fontDemo.load(..., 64)
    const float wiggleWidth = fontDemo.getWidth(wiggleText) * glyphScale;
    float penX = wcx - wiggleWidth / 2.f;

    setStrokeWeight(0.4f);
    for (size_t i = 0; i < wiggleText.size(); i++) {
        const uint32_t cp = (uint32_t)(unsigned char)wiggleText[i];
        const float yOff = 18.f * std::sin(wt * 3.f + (float)i * 0.9f);
        const string oneChar(1, wiggleText[i]);
        const float advance = fontDemo.getWidth(oneChar) * glyphScale;

        // getGlyphPath is em-normalized. Transform vertices to logical pixels
        // and the wiggle position; subpath structure is preserved so drawFill
        // still finds the holes (e, a).
        Path glyph = fontDemo.getGlyphPath(cp);
        for (Vec3& v : glyph.getVertices()) {
            v.x = v.x * em * glyphScale + penX;
            v.y = v.y * em * glyphScale + wcy + yOff;
        }

        setColor(0.95f, 0.85f, 0.55f);
        glyph.drawFill();
        setColor(0.0f, 0.0f, 0.0f, 0.55f);
        glyph.drawStroke();

        penX += advance;
    }

    setColor(0.5f);
    fontLabel.drawString("Per-glyph getGlyphPath + drawFill (holes auto-detected)",
                         p4x + 12, p4y + p4h - 22, Left, Top);

    // Footer stats
    setColor(0.45f);
    fontLabel.drawString(
        "Spin: " + to_string((int)(rad2deg(spin_)) % 360) + "°  "
        "Pulse: " + to_string(pulseScale).substr(0, 4) + "x  "
        "FPS: " + to_string((int)getFps()),
        24, H - 18, Left, Bottom);
}
