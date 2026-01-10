// =============================================================================
// shaderExample - Cross-Platform Shader Demo
// =============================================================================
// Demonstrates 4 fullscreen shader effects using FullscreenShader class.
//
// Controls:
//   1-4: Select effect
//   SPACE: Cycle effects
// =============================================================================

#include "tcApp.h"

// Shader desc getter function array
typedef const sg_shader_desc* (*ShaderDescFunc)(sg_backend);
static ShaderDescFunc shaderDescFuncs[] = {
    gradient_shader_desc,
    ripple_shader_desc,
    plasma_shader_desc,
    mouse_follow_shader_desc
};

void tcApp::setup() {
    logNotice("tcApp") << "shaderExample: Cross-Platform Shader Demo";
    logNotice("tcApp") << "  Press 1-4 to switch effects";
    logNotice("tcApp") << "  Press SPACE to cycle effects";

    // Load all 4 shaders
    for (int i = 0; i < NUM_EFFECTS; i++) {
        if (!shaders[i].load(shaderDescFuncs[i])) {
            logError("tcApp") << "Failed to load shader " << i;
            return;
        }
    }

    logNotice("tcApp") << "All 4 effects loaded successfully!";
}

void tcApp::update() {
    params.time = getElapsedTime();
    params.resolution[0] = (float)getWindowWidth();
    params.resolution[1] = (float)getWindowHeight();
    params.mouse[0] = (float)getGlobalMouseX();
    params.mouse[1] = (float)getGlobalMouseY();
}

void tcApp::draw() {
    clear(0.0f);

    if (!shaders[currentEffect].isLoaded()) {
        setColor(1.0f, 0.3f, 0.3f);
        drawBitmapString("Shader failed to load", 10, 20);
        return;
    }

    // Draw fullscreen effect
    shaders[currentEffect].setParams(params);
    shaders[currentEffect].draw();

    // Display info
    string info = "Effect " + to_string(currentEffect + 1) + ": " + getEffectName(currentEffect);
    drawBitmapStringHighlight(info, 10, 20,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
    drawBitmapStringHighlight("Press 1-4 or SPACE to change", 10, 40,
        Color(0, 0, 0, 0.7f), Color(0.7f, 0.7f, 0.7f));
}

void tcApp::keyPressed(int key) {
    if (key == '1') currentEffect = 0;
    else if (key == '2') currentEffect = 1;
    else if (key == '3') currentEffect = 2;
    else if (key == '4') currentEffect = 3;
    else if (key == ' ') {
        currentEffect = (currentEffect + 1) % NUM_EFFECTS;
    }
}

const char* tcApp::getEffectName(int index) {
    static const char* names[] = {
        "Gradient",
        "Ripple",
        "Plasma",
        "Mouse Follow"
    };
    return names[index];
}
