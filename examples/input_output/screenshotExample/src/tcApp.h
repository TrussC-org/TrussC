#pragma once

#include "TrussC.h"
#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // スクリーンショット保存先
    std::filesystem::path savePath;
    int screenshotCount = 0;

    // 画面描画内容（デモ用）
    float time = 0.0f;
};
