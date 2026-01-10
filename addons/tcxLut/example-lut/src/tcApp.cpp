// =============================================================================
// lutFilterExample - LUT (Look-Up Table) color grading demo
// =============================================================================
// Demonstrates 3D LUT color grading with 8 different color styles.
// Uses VideoGrabber for camera input and applies LUTs via GPU shader.
//
// Controls:
//   1-8: Select single LUT fullscreen
//   0 or SPACE: Return to 3x3 grid view
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    logNotice("tcApp") << "LUT Filter Example";
    logNotice("tcApp") << "  Press 1-8 to view single LUT fullscreen";
    logNotice("tcApp") << "  Press 0 or SPACE to return to grid view";

    // Initialize camera
    grabber.setDeviceID(0);
    grabber.setup(1280, 720);

    // Generate LUTs (32x32x32 size, good balance of quality/memory)
    luts[0] = createIdentity(32);
    luts[1] = createVintage(32);
    luts[2] = createCinematic(32);
    luts[3] = createFilmNoir(32);
    luts[4] = createWarm(32);
    luts[5] = createCool(32);
    luts[6] = createCyberpunk(32);

    // Load custom LUT from .cube file (demonstrating file loading)
#ifdef __EMSCRIPTEN__
    // Web: use generated version (file loading requires async fetch)
    luts[7] = createPastel(32);
#else
    luts[7].load(getDataPath("customLut.cube"));
#endif

    // Load LUT shader (uses built-in shader from TrussC core)
    if (!lutShader.load()) {
        logError("tcApp") << "Failed to load LUT shader";
    }
}

void tcApp::update() {
    grabber.update();
}

void tcApp::draw() {
    clear(0.1f);

    if (grabber.isPendingPermission()) {
        setColor(1.0f);
        drawBitmapString("Waiting for camera permission...", 20, 30);
        return;
    }

    if (!grabber.isInitialized()) {
        setColor(1.0f);
        drawBitmapString("Camera not available.", 20, 30);
        return;
    }

    if (!lutShader.isLoaded()) {
        setColor(1.0f, 0.3f, 0.3f);
        drawBitmapString("Shader failed to load.", 20, 30);
        return;
    }

    float winW = (float)getWindowWidth();
    float winH = (float)getWindowHeight();

    // Set source texture for LUT shader
    lutShader.setTexture(grabber.getTexture());

    if (selectedLut >= 0 && selectedLut < NUM_LUTS) {
        // Fullscreen single LUT view
        drawWithLut(0, 0, winW, winH, selectedLut);

        // Show LUT name
        setColor(1.0f);
        drawBitmapStringHighlight(lutNames[selectedLut], 10, 20,
            Color(0, 0, 0, 0.7f), Color(1, 1, 1));
        drawBitmapStringHighlight("Press 0 or SPACE for grid view", 10, 40,
            Color(0, 0, 0, 0.5f), Color(0.7f, 0.7f, 0.7f));
    } else {
        // 3x3 grid view
        float cellW = winW / 3.0f;
        float cellH = winH / 3.0f;

        // Layout:
        // [Original] [Vintage]   [Cinematic]
        // [FilmNoir] [Warm]      [Cool]
        // [Cyberpunk][Pastel]    [Identity]

        // Draw LUT cells
        drawWithLut(cellW, 0, cellW, cellH, 1);           // Vintage
        drawWithLut(cellW * 2, 0, cellW, cellH, 2);       // Cinematic
        drawWithLut(0, cellH, cellW, cellH, 3);           // Film Noir
        drawWithLut(cellW, cellH, cellW, cellH, 4);       // Warm
        drawWithLut(cellW * 2, cellH, cellW, cellH, 5);   // Cool
        drawWithLut(0, cellH * 2, cellW, cellH, 6);       // Cyberpunk
        drawWithLut(cellW, cellH * 2, cellW, cellH, 7);   // Pastel
        drawWithLut(cellW * 2, cellH * 2, cellW, cellH, 0); // Identity

        // Draw Original cell (no LUT)
        drawOriginal(0, 0, cellW, cellH);

        // Draw labels
        setColor(1.0f);

        const char* labels[9] = {
            "Original", "Vintage", "Cinematic",
            "Film Noir", "Warm", "Cool",
            "Cyberpunk", "Custom (.cube)", "Identity"
        };

        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int idx = row * 3 + col;
                float x = col * cellW + 5;
                float y = row * cellH + 15;
                drawBitmapStringHighlight(labels[idx], x, y,
                    Color(0, 0, 0, 0.6f), Color(1, 1, 1));
            }
        }

        drawBitmapStringHighlight("Press 1-8 for fullscreen", 10, winH - 20,
            Color(0, 0, 0, 0.5f), Color(0.7f, 0.7f, 0.7f));
    }
}

void tcApp::drawWithLut(float x, float y, float w, float h, int lutIndex) {
    if (lutIndex < 0 || lutIndex >= NUM_LUTS) return;
    if (!luts[lutIndex].isAllocated()) return;

    lutShader.setLut(luts[lutIndex]);
    lutShader.draw(x, y, w, h);
}

void tcApp::drawOriginal(float x, float y, float w, float h) {
    // Calculate aspect-correct drawing within cell
    float srcW = (float)grabber.getWidth();
    float srcH = (float)grabber.getHeight();
    float s = std::min(w / srcW, h / srcH);
    float drawW = srcW * s;
    float drawH = srcH * s;
    float drawX = x + (w - drawW) / 2;
    float drawY = y + (h - drawH) / 2;

    setColor(1.0f);
    grabber.draw(drawX, drawY, drawW, drawH);
}

void tcApp::keyPressed(int key) {
    if (key >= '1' && key <= '8') {
        selectedLut = key - '1';
    } else if (key == '0' || key == ' ') {
        selectedLut = -1;  // Grid view
    }
}
