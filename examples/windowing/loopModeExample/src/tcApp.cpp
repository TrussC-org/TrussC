#include "tcApp.h"

void tcApp::setup() {
    tcLogNotice("tcApp") << "windowExample: Loop Architecture Demo";
    tcLogNotice("tcApp") << "  - 1: VSync (default)";
    tcLogNotice("tcApp") << "  - 2: Fixed 30 FPS";
    tcLogNotice("tcApp") << "  - 3: Fixed 5 FPS";
    tcLogNotice("tcApp") << "  - 4: Event-driven (click to redraw)";
    tcLogNotice("tcApp") << "  - 5: Decoupled Update (500Hz) + VSync Draw";
    tcLogNotice("tcApp") << "  - ESC: Quit";

    // Default: VSync
    setFps(VSYNC);
}

void tcApp::update() {
    updateCount++;
}

void tcApp::draw() {
    drawCount++;

    // Reset counter every second
    float t = getElapsedTime();
    if (t - lastResetTime >= 1.0f) {
        lastResetTime = t;
        updateCount = 0;
        drawCount = 0;
    }

    // Change background color based on mode
    switch (mode) {
        case 0: clear(0.1f, 0.1f, 0.2f); break;  // VSync: Blue
        case 1: clear(0.1f, 0.2f, 0.1f); break;  // 30fps: Green
        case 2: clear(0.2f, 0.2f, 0.1f); break;  // 5fps: Yellow
        case 3: clear(0.2f, 0.1f, 0.1f); break;  // Event: Red
        case 4: clear(0.2f, 0.1f, 0.2f); break;  // Decoupled: Purple
        default: clear(0.1f, 0.1f, 0.1f); break;
    }

    // Rotating rectangle (for animation verification)
    float angle = getElapsedTime();
    pushMatrix();
    translate(getWindowWidth() / 2, getWindowHeight() / 2);
    rotate(angle);
    setColor(1.0f, 1.0f, 1.0f);
    drawRect(-250, -50, 500, 100);
    popMatrix();

    // Display information
    setColor(1.0f, 1.0f, 1.0f);
    float y = 20;

    drawBitmapString("Loop Architecture Demo", 10, y); y += 20;
    y += 10;

    // Current mode
    string modeStr;
    switch (mode) {
        case 0: modeStr = "VSync (default)"; break;
        case 1: modeStr = "Fixed 30 FPS"; break;
        case 2: modeStr = "Fixed 5 FPS"; break;
        case 3: modeStr = "Event-driven"; break;
        case 4: modeStr = "Decoupled (Update 500Hz)"; break;
    }
    drawBitmapString("Mode: " + modeStr, 10, y); y += 16;
    y += 10;

    // Settings status
    auto fps = getFpsSettings();

    // Helper to display FPS value
    auto fpsToString = [](float f) -> string {
        if (f == VSYNC) return "VSYNC";
        if (f == EVENT_DRIVEN) return "EVENT_DRIVEN";
        return toString(f, 1);
    };

    drawBitmapString("Draw FPS: " + fpsToString(fps.drawFps), 10, y); y += 16;
    drawBitmapString("Update FPS: " + fpsToString(fps.updateFps), 10, y); y += 16;
    drawBitmapString("Synced: " + string(fps.synced ? "YES" : "NO"), 10, y); y += 16;
    if (fps.actualVsyncFps > 0) {
        drawBitmapString("VSync rate: " + toString(fps.actualVsyncFps, 0) + " Hz", 10, y); y += 16;
    }
    y += 10;

    // Actual FPS
    drawBitmapString("Actual FPS: " + toString(getFrameRate(), 1), 10, y); y += 16;
    drawBitmapString("Update/sec: " + toString(updateCount), 10, y); y += 16;
    drawBitmapString("Draw/sec: " + toString(drawCount), 10, y); y += 16;
    y += 20;

    // Controls
    drawBitmapString("Press 1-5 to change mode", 10, y); y += 16;
    if (mode == 3) {
        drawBitmapString("Click to redraw!", 10, y); y += 16;
    }
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == '1') {
        mode = 0;
        setFps(VSYNC);
        tcLogNotice("tcApp") << "Mode: VSync";
    }
    else if (key == '2') {
        mode = 1;
        setFps(30);
        tcLogNotice("tcApp") << "Mode: Fixed 30 FPS";
    }
    else if (key == '3') {
        mode = 2;
        setFps(5);
        tcLogNotice("tcApp") << "Mode: Fixed 5 FPS";
    }
    else if (key == '4') {
        mode = 3;
        setFps(EVENT_DRIVEN);
        redraw();  // Draw once immediately after mode switch
        tcLogNotice("tcApp") << "Mode: Event-driven (click to redraw)";
    }
    else if (key == '5') {
        mode = 4;
        setIndependentFps(500, VSYNC);  // Update: 500Hz, Draw: VSync
        tcLogNotice("tcApp") << "Mode: Decoupled (Update 500Hz, Draw VSync)";
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    // In event-driven mode, redraw on click
    if (mode == 3) {
        redraw();
    }
}
