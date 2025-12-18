#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>
#include <string>

using namespace std;

// =============================================================================
// blendingExample - ブレンドモードの比較デモ
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // ブレンドモード名を取得
    string getBlendModeName(BlendMode mode);

    // デモ用パラメータ
    float animTime_ = 0.0f;
};
