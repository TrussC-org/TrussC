// =============================================================================
// shaderRepro - empirical tests for suspected deferred-shader-draw bugs
//
// Mode A (frames 0..120):
//   Test 1: uniform last-write-wins   (top row)
//   Test 2: setTexture last-write-wins (middle row)
//   Test 4: pushShader inside Fbo pass (bottom-right corner)
// Mode B (frames >120):
//   Test 3: scope-local Shader constructed/destroyed every frame in draw()
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    if (!solidShader.load(solid_shader_desc)) {
        logError("tcApp") << "failed to load solid shader";
    }
    if (!texShader.load(texquad_shader_desc)) {
        logError("tcApp") << "failed to load texquad shader";
    }
    if (!fboShader.load(solid_shader_desc)) {
        logError("tcApp") << "failed to load fbo shader";
    }

    // Two solid-color 32x32 images for test 2
    imgBlue.allocate(32, 32, 4);
    imgYellow.allocate(32, 32, 4);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            imgBlue.setColor(x, y, Color(0, 0, 1, 1));
            imgYellow.setColor(x, y, Color(1, 1, 0, 1));
        }
    }
    imgBlue.update();
    imgYellow.update();

    // Fbo for test 4
    fbo.allocate(400, 300);
}

void tcApp::update() {
}

void tcApp::draw() {
    frameCount++;
    clear(0.12f);

    float winW = (float)getWidth();
    float winH = (float)getHeight();
    Vec2 screenSize(winW, winH);

    if (frameCount <= 600) {
        // =====================================================================
        // MODE A
        // =====================================================================

        // Reference markers drawn with plain pipeline:
        // white frame around each expected quad position
        setColor(colors::white);
        drawBitmapString("T1 uniform: expect LEFT=RED RIGHT=GREEN", 40, 30);
        drawBitmapString("T2 texture: expect LEFT=BLUE RIGHT=YELLOW", 40, 230);
        drawBitmapString("T4 fbo: shader quad must ONLY be in corner copy ->", 40, 430);

        // plain reference frames (slightly larger than the 100x100 quads)
        setColor(0.5f, 0.5f, 0.5f);
        drawRect(35, 45, 110, 110);    // T1 left frame
        drawRect(235, 45, 110, 110);   // T1 right frame
        drawRect(35, 245, 110, 110);   // T2 left frame
        drawRect(235, 245, 110, 110);  // T2 right frame

        // ---------------------------------------------------------------------
        // Test 1: uniform last-write-wins
        // ---------------------------------------------------------------------
        solidShader.setUniform(1, screenSize);            // vs_params
        solidShader.setUniform(0, Color(1, 0, 0, 1));     // RED
        pushShader(solidShader);
        drawRect(40, 50, 100, 100);                       // LEFT quad
        popShader();

        solidShader.setUniform(0, Color(0, 1, 0, 1));     // GREEN
        pushShader(solidShader);
        drawRect(240, 50, 100, 100);                      // RIGHT quad
        popShader();

        // ---------------------------------------------------------------------
        // Test 2: setTexture last-write-wins
        // ---------------------------------------------------------------------
        texShader.setUniform(1, screenSize);
        texShader.setTexture(0, imgBlue.getTexture().getView(),
                                imgBlue.getTexture().getSampler());
        pushShader(texShader);
        drawRect(40, 250, 100, 100);                      // LEFT quad (expect BLUE)
        popShader();

        texShader.setTexture(0, imgYellow.getTexture().getView(),
                                imgYellow.getTexture().getSampler());
        pushShader(texShader);
        drawRect(240, 250, 100, 100);                     // RIGHT quad (expect YELLOW)
        popShader();

        // ---------------------------------------------------------------------
        // Test 4: pushShader inside an Fbo pass
        // ---------------------------------------------------------------------
        fbo.begin(0.0f, 0.0f, 0.3f, 1.0f);                // dark blue bg inside fbo

        // plain 2D content inside fbo (magenta rect, local coords)
        setColor(1, 0, 1);
        drawRect(10, 10, 380, 60);

        // shader quad inside fbo at KNOWN LOCAL position (20,100)-(320,280).
        // Note the shader converts position by screenSize; pass fbo size so
        // local coords map correctly inside the fbo.
        fboShader.setUniform(1, Vec2(400, 300));
        fboShader.setUniform(0, Color(1, 0.5f, 0, 1)); // ORANGE
        pushShader(fboShader);
        drawRect(20, 100, 300, 180);
        popShader();

        fbo.end();

        // draw the fbo into a small corner region (bottom-right)
        setColor(colors::white);
        fbo.draw(700, 440, 200, 150);
        // reference frame around the corner copy
        setColor(0.5f, 0.5f, 0.5f);
        drawRect(695, 435, 2, 160);
        drawRect(903, 435, 2, 160);

        // (test 4 uses its own Shader instance so its uniforms cannot
        //  contaminate the test-1 solidShader draws)
    } else {
        // =====================================================================
        // MODE B: Test 3 — scope-local Shader use-after-free
        // =====================================================================
        setColor(colors::white);
        drawBitmapString("T3 scope-local shader (frame " + to_string(frameCount) + ")", 40, 30);
        setColor(0.5f, 0.5f, 0.5f);
        drawRect(395, 195, 210, 210); // reference frame

        {
            Shader localShader;
            if (localShader.load(solid_shader_desc)) {
                localShader.setUniform(1, screenSize);
                localShader.setUniform(0, Color(0, 1, 1, 1)); // CYAN
                pushShader(localShader);
                drawRect(400, 200, 200, 200);
                popShader();
            }
            // localShader destructs HERE, before present() executes the
            // deferred draw -> suspected use-after-free
        }
    }
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
