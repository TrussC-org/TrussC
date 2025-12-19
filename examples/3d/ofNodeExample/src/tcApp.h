#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;  // App, Node, TrussC.h を含む
#include <cstdio>       // snprintf

// =============================================================================
// 回転するコンテナノード
// 子ノードが追加される親ノード。マウスのローカル座標をテストできる
// =============================================================================
class RotatingContainer : public Node {
public:
    float rotationSpeed = 0.5f;
    float size = 200.0f;

    void update() override {
        rotation += (float)getDeltaTime() * rotationSpeed;
    }

    void draw() override {
        // コンテナの境界を表示（ローカル座標 0,0 が中心）
        noFill();
        stroke();
        setColor(0.5f, 0.5f, 0.5f);
        drawRect(-size/2, -size/2, size, size);
        fill();
        noStroke();

        // 中心点
        setColor(1.0f, 1.0f, 0.0f);
        drawCircle(0, 0, 5);

        // ローカル座標軸を表示
        setColor(1.0f, 0.3f, 0.3f);  // X軸 = 赤
        drawLine(0, 0, 50, 0);
        setColor(0.3f, 1.0f, 0.3f);  // Y軸 = 緑
        drawLine(0, 0, 0, 50);

        // タイトル（四角形の上部に表示）
        setColor(1.0f, 1.0f, 1.0f, 0.8f);
        drawBitmapString("Local Coord System", -size/2, -size/2 - 12, false);
    }
};

// =============================================================================
// マウス追従ノード（ローカル座標を使用）
// =============================================================================
class MouseFollower : public Node {
public:
    float radius = 15.0f;
    float r = 0.3f, g = 0.7f, b = 1.0f;

    void draw() override {
        // getMouseX/Y() は親の変換を考慮したローカル座標を返す
        // 親が回転していても、正しい位置に描画される
        float mx = getMouseX();
        float my = getMouseY();

        // 親コンテナのサイズ（RotatingContainerのsize/2が境界）
        float bound = 125.0f;  // コンテナサイズの半分程度
        bool insideBox = (mx >= -bound && mx <= bound && my >= -bound && my <= bound);

        setColor(r, g, b, 0.8f);
        drawCircle(mx, my, radius);

        // 中心点
        setColor(1.0f, 1.0f, 1.0f);
        drawCircle(mx, my, 3);

        // 四角形の中にいる時だけローカル座標を表示
        if (insideBox) {
            char buf[64];
            snprintf(buf, sizeof(buf), "local: %.0f, %.0f", mx, my);
            setColor(1.0f, 1.0f, 1.0f, 0.9f);
            drawBitmapString(buf, mx, my);
        }
    }
};

// =============================================================================
// 固定位置の子ノード（ローカル座標で配置）
// =============================================================================
class FixedChild : public Node {
public:
    float size = 30.0f;
    float hue = 0.0f;

    void draw() override {
        // hue に基づいて色を設定
        float r = (sin(hue) * 0.5f + 0.5f);
        float g = (sin(hue + TAU / 3) * 0.5f + 0.5f);
        float b = (sin(hue + TAU * 2 / 3) * 0.5f + 0.5f);

        setColor(r, g, b);
        drawRect(-size/2, -size/2, size, size);
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
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;

private:
    // ノード
    std::shared_ptr<RotatingContainer> container1_;
    std::shared_ptr<RotatingContainer> container2_;
    std::shared_ptr<MouseFollower> follower1_;
    std::shared_ptr<MouseFollower> follower2_;
};
