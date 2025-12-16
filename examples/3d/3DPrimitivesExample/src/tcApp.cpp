#include "tcApp.h"

using namespace std;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    tc::tcLogNotice() << "05_3d_primitives: 3D Primitives Demo";
    tc::tcLogNotice() << "  - 1/2/3/4: 解像度変更";
    tc::tcLogNotice() << "  - s: 塗りつぶし ON/OFF";
    tc::tcLogNotice() << "  - w: ワイヤーフレーム ON/OFF";
    tc::tcLogNotice() << "  - l: ライティング ON/OFF";
    tc::tcLogNotice() << "  - ESC: 終了";

    // ライト設定（斜め上から照らす平行光源）
    light_.setDirectional(tc::Vec3(-1, -1, -1));
    light_.setAmbient(0.2f, 0.2f, 0.25f);
    light_.setDiffuse(1.0f, 1.0f, 0.95f);
    light_.setSpecular(1.0f, 1.0f, 1.0f);

    // 各プリミティブ用のマテリアル
    materials_[0] = tc::Material::plastic(tc::Color(0.8f, 0.2f, 0.2f));  // Plane: 赤
    materials_[1] = tc::Material::gold();                                  // Box: ゴールド
    materials_[2] = tc::Material::plastic(tc::Color(0.2f, 0.6f, 0.9f));  // Sphere: 青
    materials_[3] = tc::Material::emerald();                              // IcoSphere: エメラルド
    materials_[4] = tc::Material::silver();                               // Cylinder: シルバー
    materials_[5] = tc::Material::copper();                               // Cone: 銅

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

    tc::tcLogNotice() << "Resolution mode: " << resolution;
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

    // 3D描画モードを有効化（パースペクティブ + 深度テスト）
    tc::enable3DPerspective(tc::radians(45.0f), 0.1f, 100.0f);

    float t = tc::getElapsedTime();

    // oFと同じ回転計算（マウス押下時は停止）
    float spinX = 0, spinY = 0;
    if (!tc::isMousePressed()) {
        spinX = sin(t * 0.35f);
        spinY = cos(t * 0.075f);
    }

    struct PrimitiveInfo {
        tc::Mesh* mesh;
        const char* name;
        float x, y;  // 3D空間での位置（-1〜1 範囲）
    };

    // 3x2 グリッドに配置（パースペクティブ空間）
    PrimitiveInfo primitives[] = {
        { &plane,     "Plane",      -3.0f, 1.5f },
        { &box,       "Box",         0.0f, 1.5f },
        { &sphere,    "Sphere",      3.0f, 1.5f },
        { &icoSphere, "IcoSphere",  -3.0f, -1.5f },
        { &cylinder,  "Cylinder",    0.0f, -1.5f },
        { &cone,      "Cone",        3.0f, -1.5f },
    };

    // ライティング設定
    if (bLighting) {
        tc::enableLighting();
        tc::addLight(light_);
        // カメラ位置を設定（スペキュラー計算用）
        tc::setCameraPosition(0, 0, 0);
    }

    // 各プリミティブを描画
    for (int i = 0; i < 6; i++) {
        auto& p = primitives[i];

        tc::pushMatrix();
        tc::translate(p.x, p.y, -8.0f);

        // 3D回転（oFと同じようにX軸とY軸で回転）
        tc::rotateY(spinX);
        tc::rotateX(spinY);

        // スケールを小さく（パースペクティブ用）
        tc::scale(0.01f, 0.01f, 0.01f);

        // 塗りつぶし
        if (bFill) {
            if (bLighting) {
                // ライティング使用時はマテリアルを設定
                tc::setMaterial(materials_[i]);
                tc::setColor(1.0f, 1.0f, 1.0f);  // 白で描画（マテリアルが色を決定）
            } else {
                // ライティングなし時は従来の色
                float hue = (float)i / 6.0f * tc::TAU;
                tc::setColor(
                    0.5f + 0.4f * cos(hue),
                    0.5f + 0.4f * cos(hue + tc::TAU / 3),
                    0.5f + 0.4f * cos(hue + tc::TAU * 2 / 3)
                );
            }
            p.mesh->draw();
        }

        // ワイヤーフレーム（ライティングなしで描画）
        if (bWireframe) {
            tc::disableLighting();
            tc::setColor(0.0f, 0.0f, 0.0f);
            p.mesh->drawWireframe();
            if (bLighting) {
                tc::enableLighting();
                tc::addLight(light_);
            }
        }

        tc::popMatrix();
    }

    // ライティング終了
    tc::disableLighting();
    tc::clearLights();

    // 2D描画に戻す
    tc::disable3D();

    // 操作説明（左上）
    tc::setColor(1.0f, 1.0f, 1.0f);
    float y = 20;
    tc::drawBitmapString("3D Primitives Demo", 10, y); y += 16;
    tc::drawBitmapString("1-4: Resolution (" + tc::toString(resolution) + ")", 10, y); y += 16;
    tc::drawBitmapString("s: Fill " + string(bFill ? "[ON]" : "[OFF]"), 10, y); y += 16;
    tc::drawBitmapString("w: Wireframe " + string(bWireframe ? "[ON]" : "[OFF]"), 10, y); y += 16;
    tc::drawBitmapString("l: Lighting " + string(bLighting ? "[ON]" : "[OFF]"), 10, y); y += 16;
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
        tc::tcLogNotice() << "Fill: " << (bFill ? "ON" : "OFF");
    }
    else if (key == 'w' || key == 'W') {
        bWireframe = !bWireframe;
        tc::tcLogNotice() << "Wireframe: " << (bWireframe ? "ON" : "OFF");
    }
    else if (key == 'l' || key == 'L') {
        bLighting = !bLighting;
        tc::tcLogNotice() << "Lighting: " << (bLighting ? "ON" : "OFF");
    }
}
