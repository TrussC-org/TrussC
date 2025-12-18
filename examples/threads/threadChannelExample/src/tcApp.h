#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "AnalysisThread.h"

// =============================================================================
// tcApp - ThreadChannel サンプルアプリケーション
// =============================================================================
//
// ThreadChannel を使ったスレッド間通信のデモ。
// メインスレッドでパターンを生成し、ワーカースレッドで解析処理。
//
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    // パターン生成用データ
    std::vector<float> sourcePixels_;
    int frameNum_ = 0;

    // 解析スレッド
    AnalysisThread analyzer_;
};
