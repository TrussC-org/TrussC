#include "tcApp.h"
#include <sstream>

void tcApp::setup() {
    setWindowTitle("easyCamExample");

    // カメラの初期設定
    cam.setDistance(600);
    cam.setTarget(0, 0, 0);

    // メッシュを生成
    boxMesh = createBox(100);
    sphereMesh = createSphere(50, 24);
    coneMesh = createCone(50, 100, 24);
    cylinderMesh = createCylinder(50, 100, 24);
}

void tcApp::update() {
    // 特に処理なし
}

void tcApp::draw() {
    clear(20);

    // --- 3D描画（カメラ有効） ---
    cam.begin();

    // 右: 赤いコーン
    pushMatrix();
    translate(150, 0, 0);
    setColor(colors::red);
    coneMesh.draw();
    popMatrix();

    // 左: オレンジの球
    pushMatrix();
    translate(-150, 0, 0);
    setColor(colors::orange);
    sphereMesh.draw();
    popMatrix();

    // 下: 青いボックス
    pushMatrix();
    translate(0, 150, 0);
    setColor(colors::blue);
    boxMesh.draw();
    popMatrix();

    // 上: シアンのシリンダー
    pushMatrix();
    translate(0, -150, 0);
    setColor(colors::cyan);
    cylinderMesh.draw();
    popMatrix();

    // 前: 黄色いボックス
    pushMatrix();
    translate(0, 0, 150);
    setColor(colors::yellow);
    boxMesh.draw();
    popMatrix();

    // 後: マゼンタのボックス
    pushMatrix();
    translate(0, 0, -150);
    setColor(colors::magenta);
    boxMesh.draw();
    popMatrix();

    // グリッドを描画
    setColor(100, 100, 100);
    drawGrid(400, 10);

    cam.end();

    // --- 2D描画（UI） ---
    setColor(255);

    if (showHelp) {
        std::stringstream ss;
        ss << "FPS: " << (int)getFrameRate() << "\n\n";
        ss << "MOUSE INPUT: " << (cam.isMouseInputEnabled() ? "ON" : "OFF") << "\n";
        ss << "Distance: " << (int)cam.getDistance() << "\n";
        ss << "\n";
        ss << "Controls:\n";
        ss << "  LEFT DRAG: rotate camera\n";
        ss << "  MIDDLE DRAG: pan camera\n";
        ss << "  SCROLL: zoom in/out\n";
        ss << "\n";
        ss << "Keys:\n";
        ss << "  c: toggle mouse input\n";
        ss << "  r: reset camera\n";
        ss << "  f: toggle fullscreen\n";
        ss << "  h: toggle this help\n";

        drawBitmapString(ss.str(), 20, 20);
    }
}

void tcApp::drawGrid(float size, int divisions) {
    float step = size / divisions;
    float halfSize = size / 2.0f;

    sgl_begin_lines();
    auto col = getColor();
    sgl_c4f(col.r, col.g, col.b, col.a);

    // X軸に平行な線（Z方向に並ぶ）
    for (int i = 0; i <= divisions; i++) {
        float z = -halfSize + i * step;
        sgl_v3f(-halfSize, 0, z);
        sgl_v3f(halfSize, 0, z);
    }

    // Z軸に平行な線（X方向に並ぶ）
    for (int i = 0; i <= divisions; i++) {
        float x = -halfSize + i * step;
        sgl_v3f(x, 0, -halfSize);
        sgl_v3f(x, 0, halfSize);
    }

    sgl_end();
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case 'c':
        case 'C':
            if (cam.isMouseInputEnabled()) {
                cam.disableMouseInput();
            } else {
                cam.enableMouseInput();
            }
            break;
        case 'r':
        case 'R':
            cam.reset();
            cam.setDistance(600);
            break;
        case 'f':
        case 'F':
            toggleFullscreen();
            break;
        case 'h':
        case 'H':
            showHelp = !showHelp;
            break;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    cam.mousePressed(x, y, button);
}

void tcApp::mouseReleased(int x, int y, int button) {
    cam.mouseReleased(x, y, button);
}

void tcApp::mouseDragged(int x, int y, int button) {
    cam.mouseDragged(x, y, button);
}

void tcApp::mouseScrolled(float dx, float dy) {
    cam.mouseScrolled(dx, dy);
}
