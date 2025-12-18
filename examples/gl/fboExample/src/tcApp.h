#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

// =============================================================================
// fboExample - FBO（フレームバッファオブジェクト）サンプル
// =============================================================================
// - FBO にオフスクリーン描画して画面に表示
// - FBO 内での clear() の動作テスト
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Fbo fbo_;
    float time_ = 0;
    bool useClearInFbo_ = false;  // FBO 内で clear() を呼ぶかどうか
    bool test1Done_ = false;
    bool test2Done_ = false;
};
