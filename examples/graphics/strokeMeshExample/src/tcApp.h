#pragma once

#include "tcBaseApp.h"
using namespace tc;

using namespace trussc;

// strokeMeshExample - StrokeMesh（太線描画）のデモ
// Cap Types: BUTT, ROUND, SQUARE
// Join Types: MITER, ROUND, BEVEL
// 可変幅ストローク対応

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    std::vector<StrokeMesh> strokes;        // 開いた形状（3x3グリッド）
    std::vector<StrokeMesh> closedStrokes;  // 閉じた形状（星型）
    StrokeMesh variableStroke;              // 可変幅ストローク

    float strokeWidth = 20.0f;
};
