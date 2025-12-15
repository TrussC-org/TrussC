#pragma once

#include "tcBaseApp.h"

// =============================================================================
// tcApp - アプリケーションクラス
// tc::App を継承して、必要なメソッドをオーバーライドする
// =============================================================================

class tcApp : public tc::App {
public:
    // ライフサイクル
    void setup() override;
    void update() override;
    void draw() override;

    // 入力イベント（必要なものだけオーバーライド）
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;

private:
    tc::Polyline wave;  // テスト用
};
