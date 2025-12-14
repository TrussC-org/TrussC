#pragma once

#include "tcBaseApp.h"  // tc::App, tc::Node, TrussC.h を含む

// =============================================================================
// 回転するコンテナノード
// 子ノードが追加される親ノード。マウスのローカル座標をテストできる
// =============================================================================
class RotatingContainer : public tc::Node {
public:
    float rotationSpeed = 0.5f;
    float size = 200.0f;

    void update() override {
        rotation += (float)tc::getDeltaTime() * rotationSpeed;
    }

    void draw() override {
        // コンテナの境界を表示（ローカル座標 0,0 が中心）
        tc::noFill();
        tc::stroke();
        tc::setColor(0.5f, 0.5f, 0.5f);
        tc::drawRect(-size/2, -size/2, size, size);
        tc::fill();
        tc::noStroke();

        // 中心点
        tc::setColor(1.0f, 1.0f, 0.0f);
        tc::drawCircle(0, 0, 5);

        // ローカル座標軸を表示
        tc::setColor(1.0f, 0.3f, 0.3f);  // X軸 = 赤
        tc::drawLine(0, 0, 50, 0);
        tc::setColor(0.3f, 1.0f, 0.3f);  // Y軸 = 緑
        tc::drawLine(0, 0, 0, 50);
    }
};

// =============================================================================
// マウス追従ノード（ローカル座標を使用）
// =============================================================================
class MouseFollower : public tc::Node {
public:
    float radius = 15.0f;
    float r = 0.3f, g = 0.7f, b = 1.0f;

    void draw() override {
        // getMouseX/Y() は親の変換を考慮したローカル座標を返す
        // 親が回転していても、正しい位置に描画される
        float mx = getMouseX();
        float my = getMouseY();

        tc::setColor(r, g, b, 0.8f);
        tc::drawCircle(mx, my, radius);

        // 中心点
        tc::setColor(1.0f, 1.0f, 1.0f);
        tc::drawCircle(mx, my, 3);
    }
};

// =============================================================================
// 固定位置の子ノード（ローカル座標で配置）
// =============================================================================
class FixedChild : public tc::Node {
public:
    float size = 30.0f;
    float hue = 0.0f;

    void draw() override {
        // hue に基づいて色を設定
        float r = (sin(hue) * 0.5f + 0.5f);
        float g = (sin(hue + tc::TWO_PI / 3) * 0.5f + 0.5f);
        float b = (sin(hue + tc::TWO_PI * 2 / 3) * 0.5f + 0.5f);

        tc::setColor(r, g, b);
        tc::drawRect(-size/2, -size/2, size, size);
    }
};

// =============================================================================
// メインアプリ
// =============================================================================
class tcApp : public tc::App {
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
