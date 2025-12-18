#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

// =============================================================================
// カスタムボタン（クリックでカウントアップ）
// =============================================================================
class CounterButton : public RectNode {
public:
    using Ptr = shared_ptr<CounterButton>;

    int count = 0;
    string label = "Button";
    Color baseColor = Color(0.3f, 0.3f, 0.4f);
    Color hoverColor = Color(0.4f, 0.4f, 0.6f);
    Color pressColor = Color(0.2f, 0.2f, 0.3f);

    bool isHovered = false;
    bool isPressed = false;

    CounterButton() {
        enableEvents();  // イベントを有効化
        width = 150;
        height = 50;
    }

    void draw() override {
        // 状態に応じた色
        if (isPressed) {
            setColor(pressColor);
        } else if (isHovered) {
            setColor(hoverColor);
        } else {
            setColor(baseColor);
        }

        // 背景
        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // 枠線
        noFill();
        stroke();
        setColor(0.6f, 0.6f, 0.7f);
        drawRect(0, 0, width, height);

        // ラベルとカウント（左上に配置、回転に追従）
        fill();
        noStroke();
        setColor(1.0f, 1.0f, 1.0f);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %d", label.c_str(), count);
        drawBitmapString(buf, 4, 18, false);  // screenFixed = false で回転に追従（baseline基準）
    }

protected:
    bool onMousePress(float localX, float localY, int button) override {
        (void)localX; (void)localY; (void)button;
        isPressed = true;
        count++;
        cout << label << " pressed! count = " << count << endl;
        return true;  // イベントを消費
    }

    bool onMouseRelease(float localX, float localY, int button) override {
        (void)localX; (void)localY; (void)button;
        isPressed = false;
        return true;
    }
};

// =============================================================================
// 回転するコンテナ（RectNode を継承して当たり判定も持つ）
// =============================================================================
class RotatingPanel : public RectNode {
public:
    using Ptr = shared_ptr<RotatingPanel>;

    float rotationSpeed = 0.3f;
    Color panelColor = Color(0.2f, 0.25f, 0.3f);

    RotatingPanel() {
        enableEvents();
        width = 300;
        height = 200;
    }

    void update() override {
        rotation += (float)getDeltaTime() * rotationSpeed;
    }

    void draw() override {
        // パネル背景
        setColor(panelColor);
        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // 枠線
        noFill();
        stroke();
        setColor(0.5f, 0.5f, 0.6f);
        drawRect(0, 0, width, height);

        // 中心マーク
        fill();
        setColor(1.0f, 1.0f, 0.0f, 0.5f);
        drawCircle(width / 2, height / 2, 5);
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
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;

private:
    // 静止ボタン
    CounterButton::Ptr button1_;
    CounterButton::Ptr button2_;
    CounterButton::Ptr button3_;

    // 回転パネル内のボタン
    RotatingPanel::Ptr panel_;
    CounterButton::Ptr panelButton1_;
    CounterButton::Ptr panelButton2_;

    bool paused_ = false;
};
