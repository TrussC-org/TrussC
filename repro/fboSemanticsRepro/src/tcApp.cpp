#include "tcApp.h"

// Deferred-rendering semantics repro app.
// Cycles 3 test modes, 180 frames (~3s) each:
//   mode 0: FBO drawn, re-rendered, drawn again in one frame
//   mode 1: mid-frame swapchain clear() after prior draws
//   mode 2: FullscreenShader then Fbo begin/end resume
// Top-left indicator: (mode+1) small stacked white rects.

void tcApp::setup() {
    fbo.allocate(200, 200);
    shaderLoaded = gradientShader.load(gradient_shader_desc);
    if (!shaderLoaded) {
        logError("tcApp") << "gradient shader failed to load";
    }
}

void tcApp::update() {
    params.time = getElapsedTime();
    params.resolution[0] = (float)getWidth();
    params.resolution[1] = (float)getHeight();
    params.mouse[0] = 0.0f;
    params.mouse[1] = 0.0f;
}

void tcApp::drawModeIndicator(int mode) {
    setColor(1.0f, 1.0f, 1.0f);
    for (int i = 0; i <= mode; i++) {
        drawRect(8, 8 + i * 26, 20, 20);
    }
}

void tcApp::draw() {
    int mode = (int)((getFrameCount() / 180) % 3);

    if (mode == 0) {
        // --- Test 1: draw FBO, re-render it, draw again in same frame ---
        clear(0.12f, 0.12f, 0.12f);

        fbo.begin();
        clear(1.0f, 0.0f, 0.0f);  // RED
        fbo.end();
        setColor(1.0f, 1.0f, 1.0f);
        fbo.draw(100, 200, 200, 200);   // LEFT: expect red

        fbo.begin();
        clear(0.0f, 0.0f, 1.0f);  // BLUE
        fbo.end();
        setColor(1.0f, 1.0f, 1.0f);
        fbo.draw(600, 200, 200, 200);   // RIGHT: expect blue
    } else if (mode == 1) {
        // --- Test 2: mid-frame swapchain clear() ---
        clear(0.0f, 0.0f, 0.0f);

        setColor(1.0f, 1.0f, 0.0f);     // YELLOW big rect, left half
        drawRect(0, 0, getWidth() / 2, getHeight());

        clear(0.2f, 0.2f, 0.2f);        // should erase the yellow if erase-semantics

        setColor(0.0f, 1.0f, 1.0f);     // CYAN small rect on right
        drawRect(650, 250, 120, 120);
    } else {
        // --- Test 3: FullscreenShader then Fbo begin/end resume ---
        clear(0.12f, 0.12f, 0.12f);

        if (shaderLoaded) {
            gradientShader.setParams(params);
            gradientShader.draw();      // fullscreen gradient
        }

        // Control: first half of the mode window skips the fbo resume entirely,
        // so the gradient alone should be visible (proves the shader renders).
        bool withFbo = (getFrameCount() % 180) >= 90;
        if (withFbo) {
            fbo.begin();
            clear(0.0f, 1.0f, 0.0f);    // GREEN
            fbo.end();
            setColor(1.0f, 1.0f, 1.0f);
            fbo.draw(getWidth() - 120, getHeight() - 120, 100, 100);  // corner copy
        }
    }

    // Mode indicator drawn last so it survives any mid-frame clear
    drawModeIndicator(mode);
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
