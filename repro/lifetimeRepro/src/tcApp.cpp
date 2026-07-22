#include "tcApp.h"

// =============================================================================
// GPU resource lifetime repro tests
//
// Region layout (960x600):
//   R1 (40,60)   scope-local Image drawn then destroyed inside draw()
//   R2 (240,60)  member Image control (identical magenta content)
//   R3 (440,60)  "HELLO ASCII 123" drawn EARLY each frame (test 2 victim)
//                growth string drawn LAST each frame >= 300 at (440,160)
//   R4 (40,320)  mipmapped dynamic Image: draw, THEN update() same frame
//   R5 (240,320) mipmapped dynamic Image control: never updated after setup
//
// Region frames are plain untextured rects (immune to all three bugs).
// =============================================================================

// Shared 16x13 fullwidth glyph bitmap: solid block (all pixels on).
// Must outlive every drawBitmapString call -> static storage.
static const uint8_t kSolidGlyph[26] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static string utf8Encode(uint32_t cp) {
    string s;
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

void tcApp::fillImageSolid(Image& img, const Color& c) {
    for (int y = 0; y < img.getHeight(); y++) {
        for (int x = 0; x < img.getWidth(); x++) {
            img.setColor(x, y, c);
        }
    }
    img.update();
}

void tcApp::drawRegionFrame(float x, float y, float w, float h, const Color& c) {
    setColor(c);
    drawRect(x - 4, y - 4, w + 8, h + 8);   // border
    setColor(0.12f, 0.12f, 0.12f);
    drawRect(x, y, w, h);                   // interior (background color)
}

void tcApp::setup() {
    // Allocate only. Pixel upload happens inside draw() on frame 2, so the
    // GPU upload occurs within a valid render frame (setup-time uploads were
    // observed to be dropped).
    memberImg.allocate(64, 64, 4);
    mipImg.allocate(64, 64, 4, /*mipmaps=*/true);
    mipImgControl.allocate(64, 64, 4, /*mipmaps=*/true);
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);
    uint64_t frame = getFrameCount();

    // One-time pixel upload for controls, at the TOP of a draw frame so the
    // recorded draws below reference the post-update view.
    if (frame == 2) {
        fillImageSolid(memberImg, Color(1.0f, 0.0f, 1.0f));       // magenta
        fillImageSolid(mipImgControl, Color(1.0f, 1.0f, 0.0f));   // yellow
    }
    if (frame == 3) {
        // mipImg initial fill on its own frame (avoid same-frame double load
        // with the per-frame update below)
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                mipImg.setColor(x, y, Color(0.0f, 1.0f, 1.0f));   // cyan
            }
        }
        mipImg.update();
    }

    // --- Region frames (untextured, always visible) ---
    drawRegionFrame(40, 60, 160, 160, Color(1.0f, 0.0f, 0.0f));    // R1 red
    drawRegionFrame(240, 60, 160, 160, Color(0.0f, 1.0f, 0.0f));   // R2 green
    drawRegionFrame(440, 60, 460, 160, Color(0.0f, 0.4f, 1.0f));   // R3 blue
    drawRegionFrame(40, 320, 160, 160, Color(1.0f, 0.5f, 0.0f));   // R4 orange
    drawRegionFrame(240, 320, 160, 160, Color(1.0f, 1.0f, 1.0f));  // R5 white

    setColor(1.0f, 1.0f, 1.0f);

    // --- Test 2 (early part): plain ASCII text drawn FIRST ---
    drawBitmapString("HELLO ASCII 123", 460, 100);

    // Controls: always drawn, no destructive pattern ever touches them.
    memberImg.draw(272, 92, 96, 96);      // region 2
    mipImgControl.draw(272, 352, 96, 96); // region 5

    // R4: mipmapped dynamic image, drawn every frame
    mipImg.draw(72, 352, 96, 96);         // region 4

    // === Phases (display runs at ~120fps) ===
    //  A : 0-479       (0-4s)   controls only
    //  B1: 480-959     (4-8s)   test 1 only (scope-local Image destruct)
    //  B2: 960-1439    (8-12s)  test 3 only (mip loadData after draw)
    //  G : 1440-2160   (12-18s) test 2 only (atlas growth EVERY frame)
    //  C : 2160+                steady state
    const bool phaseB1 = (frame >= 480 && frame < 960);
    const bool phaseB2 = (frame >= 960 && frame < 1440);
    const bool phaseG  = (frame >= 1440 && frame <= 2160);
    if (frame == 480)  logNotice() << "[repro] phase B1 start (test1)";
    if (frame == 960)  logNotice() << "[repro] phase B2 start (test3)";
    if (frame == 1440) logNotice() << "[repro] phase G start (test2 growth)";
    if (frame == 2161) logNotice() << "[repro] phase C start (steady)";

    // --- Test 1: scope-local Image drawn then destroyed at scope end ---
    if (phaseB1) {
        Image localImg;
        localImg.allocate(64, 64, 4);
        fillImageSolid(localImg, Color(1.0f, 0.0f, 1.0f));  // magenta
        setColor(1.0f, 1.0f, 1.0f);
        localImg.draw(72, 92, 96, 96);   // region 1
    }  // localImg destructs here, BEFORE end-of-frame sgl flush

    // --- Test 3: update AFTER the draw. Mipmapped+dynamic loadData
    // destroys+recreates the view that mipImg.draw() above recorded. ---
    if (phaseB2) {
        // Animate so dirty_ is set every frame
        float t = fmodf(getElapsedTimef(), 1.0f);
        Color c(0.0f, 1.0f, 1.0f - t * 0.2f);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                mipImg.setColor(x, y, c);
            }
        }
        mipImg.update();
    }

    // --- Test 2 (late part): force font atlas growth mid-frame ---
    // During phase G register a batch of 64 fullwidth glyphs (= 2 atlas
    // rows) EVERY frame, then draw a string containing the newest codepoint.
    // That makes every frame in the window a growth frame (mid-frame
    // destroy+recreate of the atlas image+view).
    bool grewThisFrame = false;
    if (phaseG && (frame % 4 == 0)) {
        // 32 fullwidth glyphs = 64 cells = 1 atlas row per registration ->
        // every registration frame is an atlas GROWTH frame (until the
        // 32-row cap is hit).
        for (int i = 0; i < 32; i++) {
            bitmapfont::Glyph g;
            g.codepoint = nextCodepoint++;
            g.data = kSolidGlyph;
            g.width = bitmapfont::Width::Fullwidth;
            bitmapfont::registerGlyph(g);
        }
        lastRegisteredCp = nextCodepoint - 1;
        batchesRegistered++;
        grewThisFrame = true;
    }
    if (lastRegisteredCp != 0) {
        // Draw a string containing the newest codepoint -> triggers
        // ensureFontAtlas growth on registration frames.
        string s = utf8Encode(lastRegisteredCp) + utf8Encode(lastRegisteredCp - 1)
                 + utf8Encode(lastRegisteredCp - 2) + " GROWN";
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(s, 460, 160);
    }

    // --- Test 4 (issue #188): register FAR beyond atlas capacity ---
    // 1200 fullwidth glyphs = 2400 cells -> needs ~47 atlas rows, way past
    // the 32-row cap. Previously ensureFontAtlas computed the upload size
    // from the unclamped row count -> sg_update_image read past the CPU
    // buffer -> SIGBUS. Now it must clamp, warn once, and keep running
    // (overflow glyphs render blank).
    if (frame == 2400) {
        logNotice() << "[repro] test4: registering 1200 fullwidth glyphs (overflow)";
        for (int i = 0; i < 1200; i++) {
            bitmapfont::Glyph g;
            g.codepoint = nextCodepoint++;
            g.data = kSolidGlyph;
            g.width = bitmapfont::Width::Fullwidth;
            bitmapfont::registerGlyph(g);
        }
        lastRegisteredCp = nextCodepoint - 1;
    }

    // Untextured marker drawn ONLY on registration (growth) frames, LAST in
    // the frame. Any screenshot showing this marker is an exact growth
    // frame -> all textured draws above must still be visible in it.
    if (grewThisFrame) {
        setColor(1.0f, 0.0f, 1.0f);
        drawRect(700, 250, 24, 24);
    }

    // Frame counter (helps correlate screenshots with phases)
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("frame " + to_string(frame), 40, 560);

    // Untextured batch indicator: moves right on the exact growth frame.
    // Immune to all texture bugs -> marks growth frames in video captures.
    setColor(1.0f, 1.0f, 1.0f);
    drawRect(440.0f + batchesRegistered * 30.0f, 250.0f, 20, 20);
}

void tcApp::keyPressed(int key) {}
void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(const MouseEventArgs& e) {}
void tcApp::mouseReleased(const MouseEventArgs& e) {}
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {}
void tcApp::mouseDragged(const MouseDragEventArgs& e) {}
void tcApp::mouseScrolled(const ScrollEventArgs& e) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
