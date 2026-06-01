// =============================================================================
// coordinateConversionExample
// Demonstrates worldToScreen and screenToWorld:
//   - worldToScreen: 3D object positions -> 2D screen labels
//   - screenToWorld: mouse click -> 3D marker placement on Z=0 plane
// Note: These functions use the current view/projection matrices,
//       so call them between cam.begin() and cam.end().
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Coordinate Conversion Example");
    cam.setDistance(400);
    cam.enableMouseInput();  // Auto-subscribe to mouse events
    boxMesh = createBox(60);
    sphereMesh = createSphere(40, 24);
}

void tcApp::draw() {
    clear(0.1f, 0.1f, 0.15f);

    cam.begin();

    // Draw objects
    pushMatrix();
    translate(boxPos);
    setColor(0.9f, 0.3f, 0.3f);
    boxMesh.draw();
    popMatrix();

    pushMatrix();
    translate(spherePos);
    setColor(0.3f, 0.9f, 0.3f);
    sphereMesh.draw();
    popMatrix();

    // Process pending click (screenToWorld needs 3D matrices)
    if (pendingClick) {
        clickWorldPos = screenToWorld(clickScreenPos, 0);
        hasClick = true;
        pendingClick = false;
    }

    if (hasClick) {
        setColor(1.0f, 1.0f, 0.0f);
        drawCircle(clickWorldPos, 15);
    }

    // Calculate screen positions while 3D matrices are active
    Vec3 boxScreen = worldToScreen(boxPos);
    Vec3 sphereScreen = worldToScreen(spherePos);

    cam.end();

    // 2D labels
    setColor(1.0f, 0.5f, 0.5f);
    drawBitmapString("Box", boxScreen.x - 10, boxScreen.y - 50);

    setColor(0.5f, 1.0f, 0.5f);
    drawBitmapString("Sphere", sphereScreen.x - 20, sphereScreen.y - 50);

    setColor(1.0f);
    drawBitmapString("Click to place marker (screenToWorld)", 20, 20);
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) sapp_request_quit();
}

void tcApp::mousePressed(const MouseEventArgs& e) {
    // Record click for screenToWorld demo (cam handles its own input)
    if (e.button == MOUSE_BUTTON_LEFT) {
        clickScreenPos = e.pos;
        pendingClick = true;
    }
}
