#include "tcApp.h"
#include <iostream>

using namespace std;

void tcApp::setup() {
    cout << "windowExample: Loop Architecture Demo" << endl;
    cout << "  - 1: VSync (default)" << endl;
    cout << "  - 2: Fixed 30 FPS" << endl;
    cout << "  - 3: Fixed 5 FPS" << endl;
    cout << "  - 4: Event-driven (click to redraw)" << endl;
    cout << "  - 5: Decoupled Update (500Hz) + VSync Draw" << endl;
    cout << "  - ESC: Quit" << endl;

    // Default: VSync
    setVsync(true);
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
    drawBitmapString("Draw VSync: " + string(isDrawVsync() ? "ON" : "OFF"), 10, y); y += 16;
    drawBitmapString("Draw FPS setting: " + toString(getDrawFps()), 10, y); y += 16;
    drawBitmapString("Update synced: " + string(isUpdateSyncedToDraw() ? "YES" : "NO"), 10, y); y += 16;
    drawBitmapString("Update FPS setting: " + toString(getUpdateFps()), 10, y); y += 16;
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
        setVsync(true);
        cout << "Mode: VSync" << endl;
    }
    else if (key == '2') {
        mode = 1;
        setFps(30);
        cout << "Mode: Fixed 30 FPS" << endl;
    }
    else if (key == '3') {
        mode = 2;
        setFps(5);
        cout << "Mode: Fixed 5 FPS" << endl;
    }
    else if (key == '4') {
        mode = 3;
        setDrawFps(0);  // Stop automatic drawing
        syncUpdateToDraw(true);
        redraw();  // Draw once immediately after mode switch
        cout << "Mode: Event-driven (click to redraw)" << endl;
    }
    else if (key == '5') {
        mode = 4;
        setDrawVsync(true);
        setUpdateFps(500);  // Update runs independently at 500Hz
        cout << "Mode: Decoupled (Update 500Hz, Draw VSync)" << endl;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    // In event-driven mode, redraw on click
    if (mode == 3) {
        redraw();
    }
}
