#pragma once

#include "tcBaseApp.h"

// =============================================================================
// tcApp - 3Dプリミティブデモ
// oFの3DPrimitivesExampleを参考にしたシンプル版
// =============================================================================

class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // プリミティブのメッシュ
    tc::Mesh plane;
    tc::Mesh box;
    tc::Mesh sphere;
    tc::Mesh icoSphere;
    tc::Mesh cylinder;
    tc::Mesh cone;

    // 描画モード
    bool bFill = true;
    bool bWireframe = true;

    // 解像度モード (1-4)
    int resolution = 2;

    // プリミティブを再生成
    void rebuildPrimitives();
};
