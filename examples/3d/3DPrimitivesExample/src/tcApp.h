#pragma once

#include "tcBaseApp.h"
using namespace tc;

// =============================================================================
// tcApp - 3Dプリミティブデモ
// oFの3DPrimitivesExampleを参考にしたシンプル版
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // プリミティブのメッシュ
    Mesh plane;
    Mesh box;
    Mesh sphere;
    Mesh icoSphere;
    Mesh cylinder;
    Mesh cone;

    // 描画モード
    bool bFill = true;
    bool bWireframe = true;
    bool bLighting = true;

    // 解像度モード (1-4)
    int resolution = 2;

    // ライティング
    Light light_;
    Material materials_[6];  // 各プリミティブ用マテリアル

    // プリミティブを再生成
    void rebuildPrimitives();
};
