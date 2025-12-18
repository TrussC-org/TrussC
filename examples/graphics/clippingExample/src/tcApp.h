#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

// =============================================================================
// ClipBox - クリッピング有効な矩形ノード
// =============================================================================
class ClipBox : public RectNode {
public:
    using Ptr = shared_ptr<ClipBox>;

    Color bgColor = Color(0.2f, 0.2f, 0.25f);
    Color borderColor = Color(0.5f, 0.5f, 0.6f);
    string label;

    ClipBox() {
        setClipping(true);  // クリッピング有効
    }

    void draw() override {
        // 背景
        setColor(bgColor);
        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // ラベル
        if (!label.empty()) {
            setColor(1.0f, 1.0f, 1.0f, 0.7f);
            drawBitmapString(label, 5, 15, false);
        }
    }

    // 枠線は子を描画した後に描きたいので別メソッド
    void drawBorder() {
        noFill();
        stroke();
        setColor(borderColor);
        drawRect(0, 0, width, height);
    }
};

// =============================================================================
// BouncingCircle - 跳ね回る円（クリッピングテスト用）
// =============================================================================
class BouncingCircle : public RectNode {
public:
    using Ptr = shared_ptr<BouncingCircle>;

    float radius = 30;
    Color color = Color(0.8f, 0.4f, 0.2f);
    float vx = 2.0f;
    float vy = 1.5f;
    float boundsWidth = 300;
    float boundsHeight = 200;

    void update() override {
        // 移動
        x += vx;
        y += vy;

        // 跳ね返り
        if (x - radius < -50 || x + radius > boundsWidth + 50) vx = -vx;
        if (y - radius < -50 || y + radius > boundsHeight + 50) vy = -vy;
    }

    void draw() override {
        setColor(color);
        fill();
        noStroke();
        drawCircle(0, 0, radius);

        // 中心点
        setColor(1.0f, 1.0f, 1.0f);
        drawCircle(0, 0, 3);
    }
};

// =============================================================================
// メインアプリ
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // 外側のクリップボックス
    ClipBox::Ptr outerBox_;

    // 内側のクリップボックス（回転する）
    ClipBox::Ptr innerBox_;

    // 跳ね回る円たち
    vector<BouncingCircle::Ptr> circles_;
};
