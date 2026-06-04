#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("tcxDepthCamera - SyntheticDepthCamera point cloud");

    camera = make_shared<SyntheticDepthCamera>(640, 480);
    camera->setThreaded(true);   // grab on a background thread (before setup)
    camera->enableDepth();       // streams are all off by default; enable what we use
    camera->enableColor();
    camera->setup();

    view.setDistance(400.0f);
    view.enableMouseInput();
}

void tcApp::update() {
    camera->update();
    if (camera->isFrameNew()) rebuild();
}

// Rebuild the point-cloud mesh from the latest frame, in display units.
void tcApp::rebuild() {
    cloud = camera->toMesh({.colors = colored, .step = step});
    // tcxDepthCamera world coords are in meters with +Y down. Scale up for the
    // viewer, flip Y up, and pull the wall (~2.5 m) back toward the origin.
    cloud.scale(100.0f, -100.0f, 100.0f);
    cloud.translate(0.0f, 0.0f, -200.0f);
}

void tcApp::draw() {
    clear(0.08f, 0.09f, 0.11f);

    view.begin();
    pushMatrix();
    // Start a quarter-turn in so we open facing the front of the cloud.
    rotateY(getElapsedTimef() * 0.3f + TAU * 0.25f);
    if (!colored) setColor(0.55f, 0.75f, 1.0f);
    cloud.draw();
    popMatrix();
    view.end();

    // 2D stream previews along the bottom: color + depth thumbnails. Each is the
    // camera's cached getXxxImage() - no per-app upload code. Drawn one after
    // another, advancing tx. (Synthetic has no IR stream, so no IR thumbnail.)
    const float th = 130.0f, pad = 10.0f;
    const float ty = getWindowHeight() - th - pad;
    float tx = pad;
    setColor(1.0f);

    if (camera->hasColor()) {
        Image& color = camera->getColorImage();
        if (color.isAllocated()) {
            float tw = th * color.getWidth() / (float)color.getHeight();
            color.draw(tx, ty, tw, th);
            drawBitmapString("color", tx + 4, ty - 6);
            tx += tw + pad;
        }
    }

    Image& depth = camera->getDepthImage({.nearM = 1.5f, .farM = 3.5f, .repeat = repeatDepth});
    if (depth.isAllocated()) {
        float tw = th * depth.getWidth() / (float)depth.getHeight();
        depth.draw(tx, ty, tw, th);
        drawBitmapString(repeatDepth ? "depth [repeat]" : "depth", tx + 4, ty - 6);
        tx += tw + pad;
    }

    setColor(1.0f);
    string hud = "SyntheticDepthCamera (no hardware)\n";
    hud += to_string(cloud.getNumVertices()) + " points";
    hud += colored ? "  [colored]" : "  [plain]";
    hud += "\nC: toggle color    R: depth repeat    1-4: decimation    drag: orbit";
    drawBitmapString(hud, 20, 20);
}

void tcApp::keyPressed(int key) {
    if (key == 'c' || key == 'C') {
        colored = !colored;
    } else if (key == 'r' || key == 'R') {
        repeatDepth = !repeatDepth;
    } else if (key >= '1' && key <= '4') {
        step = key - '0';
    }
    rebuild();
}
