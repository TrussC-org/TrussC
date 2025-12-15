#include "tcApp.h"
#include <iostream>

using namespace std;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "05_3d_primitives: 3D Primitives Demo" << endl;
    cout << "  - 1/2/3/4: 解像度変更" << endl;
    cout << "  - s: 塗りつぶし ON/OFF" << endl;
    cout << "  - w: ワイヤーフレーム ON/OFF" << endl;
    cout << "  - ESC: 終了" << endl;

    rebuildPrimitives();
}

// ---------------------------------------------------------------------------
// プリミティブを再生成
// ---------------------------------------------------------------------------
void tcApp::rebuildPrimitives() {
    float size = 80.0f;

    int planeRes, sphereRes, icoRes, cylRes, coneRes;

    switch (resolution) {
        case 1:
            planeRes = 2; sphereRes = 4; icoRes = 0; cylRes = 4; coneRes = 4;
            break;
        case 2:
            planeRes = 4; sphereRes = 8; icoRes = 1; cylRes = 8; coneRes = 8;
            break;
        case 3:
            planeRes = 8; sphereRes = 16; icoRes = 2; cylRes = 12; coneRes = 12;
            break;
        case 4:
        default:
            planeRes = 12; sphereRes = 32; icoRes = 3; cylRes = 20; coneRes = 20;
            break;
    }

    plane = tc::createPlane(size * 1.5f, size * 1.5f, planeRes, planeRes);
    box = tc::createBox(size);
    sphere = tc::createSphere(size * 0.7f, sphereRes);
    icoSphere = tc::createIcoSphere(size * 0.7f, icoRes);
    cylinder = tc::createCylinder(size * 0.4f, size * 1.5f, cylRes);
    cone = tc::createCone(size * 0.5f, size * 1.5f, coneRes);

    cout << "Resolution mode: " << resolution << endl;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void tcApp::update() {
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    tc::clear(0.1f, 0.1f, 0.12f);

    // 3D描画モードを有効化（深度テスト ON）
    tc::enable3D();

    float t = tc::getElapsedTime();

    // 6つのプリミティブを3x2グリッドに配置
    float screenW = tc::getWindowWidth();
    float screenH = tc::getWindowHeight();

    // マウスYでZ位置を調整（画面中央が-500、カメラの前）
    float mouseY = tc::getGlobalMouseY();
    float zOffset = -500.0f + (screenH / 2 - mouseY) * 2.0f;

    // oFと同じ回転計算（マウス押下時は停止）
    float spinX = 0, spinY = 0;
    if (!tc::isMousePressed()) {
        spinX = sin(t * 0.35f);
        spinY = cos(t * 0.075f);
    }

    struct PrimitiveInfo {
        tc::Mesh* mesh;
        const char* name;
        float x, y;
    };

    PrimitiveInfo primitives[] = {
        { &plane,     "Plane",      screenW * 1/6.f, screenH * 1/3.f },
        { &box,       "Box",        screenW * 3/6.f, screenH * 1/3.f },
        { &sphere,    "Sphere",     screenW * 5/6.f, screenH * 1/3.f },
        { &icoSphere, "IcoSphere",  screenW * 1/6.f, screenH * 2/3.f },
        { &cylinder,  "Cylinder",   screenW * 3/6.f, screenH * 2/3.f },
        { &cone,      "Cone",       screenW * 5/6.f, screenH * 2/3.f },
    };

    // 各プリミティブを描画
    for (int i = 0; i < 6; i++) {
        auto& p = primitives[i];

        tc::pushMatrix();
        tc::translate(p.x, p.y, zOffset);

        // 3D回転（oFと同じようにX軸とY軸で回転）
        tc::rotateY(spinX);
        tc::rotateX(spinY);

        // 塗りつぶし
        if (bFill) {
            // プリミティブごとに異なる色
            float hue = (float)i / 6.0f * tc::TAU;
            tc::setColor(
                0.5f + 0.4f * cos(hue),
                0.5f + 0.4f * cos(hue + tc::TAU / 3),
                0.5f + 0.4f * cos(hue + tc::TAU * 2 / 3)
            );
            p.mesh->draw();
        }

        // ワイヤーフレーム
        if (bWireframe) {
            tc::setColor(0.0f, 0.0f, 0.0f);
            p.mesh->drawWireframe();
        }

        tc::popMatrix();

        // ラベル
        tc::setColor(1.0f, 1.0f, 1.0f);
        tc::drawBitmapString(p.name, p.x - 30, p.y + 100);
    }

    // 2D描画に戻す
    tc::disable3D();

    // 操作説明（左上）
    tc::setColor(1.0f, 1.0f, 1.0f);
    float y = 20;
    tc::drawBitmapString("3D Primitives Demo", 10, y); y += 16;
    tc::drawBitmapString("1-4: Resolution (" + tc::toString(resolution) + ")", 10, y); y += 16;
    tc::drawBitmapString("s: Fill " + string(bFill ? "[ON]" : "[OFF]"), 10, y); y += 16;
    tc::drawBitmapString("w: Wireframe " + string(bWireframe ? "[ON]" : "[OFF]"), 10, y); y += 16;
    tc::drawBitmapString("Mouse Y: Z offset", 10, y); y += 16;
    y += 8;
    tc::drawBitmapString("Z: " + tc::toString(zOffset, 0), 10, y); y += 16;
    tc::drawBitmapString("FPS: " + tc::toString(tc::getFrameRate(), 1), 10, y);
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (key == tc::KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == '1') {
        resolution = 1;
        rebuildPrimitives();
    }
    else if (key == '2') {
        resolution = 2;
        rebuildPrimitives();
    }
    else if (key == '3') {
        resolution = 3;
        rebuildPrimitives();
    }
    else if (key == '4') {
        resolution = 4;
        rebuildPrimitives();
    }
    else if (key == 's' || key == 'S') {
        bFill = !bFill;
        cout << "Fill: " << (bFill ? "ON" : "OFF") << endl;
    }
    else if (key == 'w' || key == 'W') {
        bWireframe = !bWireframe;
        cout << "Wireframe: " << (bWireframe ? "ON" : "OFF") << endl;
    }
}
