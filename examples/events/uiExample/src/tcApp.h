#pragma once

#include "tcBaseApp.h"
#include <iostream>
#include <functional>

using namespace std;

// =============================================================================
// UIButton - クリックでイベント発火するボタン
// =============================================================================
class UIButton : public tc::RectNode {
public:
    using Ptr = shared_ptr<UIButton>;

    string label = "Button";
    tc::Color normalColor = tc::Color(0.25f, 0.25f, 0.3f);
    tc::Color hoverColor = tc::Color(0.35f, 0.35f, 0.45f);
    tc::Color pressColor = tc::Color(0.15f, 0.15f, 0.2f);

    // クリックイベント（外部からコールバック登録）
    function<void()> onClick;

    UIButton() {
        enableEvents();
        width = 120;
        height = 40;
    }

    void update() override {
        // ホバー判定（簡易）
        float mx = getMouseX();
        float my = getMouseY();
        isHovered_ = (mx >= 0 && mx <= width && my >= 0 && my <= height);
    }

    void draw() override {
        // 状態に応じた色
        if (isPressed_) {
            tc::setColor(pressColor);
        } else if (isHovered_) {
            tc::setColor(hoverColor);
        } else {
            tc::setColor(normalColor);
        }

        tc::fill();
        tc::noStroke();
        tc::drawRect(0, 0, width, height);

        // 枠線
        tc::noFill();
        tc::stroke();
        tc::setColor(0.5f, 0.5f, 0.6f);
        tc::drawRect(0, 0, width, height);

        // ラベル
        tc::fill();
        tc::setColor(1.0f, 1.0f, 1.0f);
        float textX = width / 2 - label.length() * 4;
        tc::drawBitmapString(label, textX, height / 2 + 5, false);
    }

protected:
    bool isHovered_ = false;
    bool isPressed_ = false;

    bool onMousePress(float lx, float ly, int btn) override {
        isPressed_ = true;
        return tc::RectNode::onMousePress(lx, ly, btn);
    }

    bool onMouseRelease(float lx, float ly, int btn) override {
        if (isPressed_ && isHovered_ && onClick) {
            onClick();  // クリックイベント発火
        }
        isPressed_ = false;
        return tc::RectNode::onMouseRelease(lx, ly, btn);
    }
};

// =============================================================================
// UISlider - ドラッグ＆スクロールで値を変更するスライダー
// =============================================================================
class UISlider : public tc::RectNode {
public:
    using Ptr = shared_ptr<UISlider>;

    float value = 0.5f;  // 0.0 ~ 1.0
    float minValue = 0.0f;
    float maxValue = 1.0f;
    string label = "Slider";

    // 値変更イベント
    function<void(float)> onValueChanged;

    UISlider() {
        enableEvents();
        width = 200;
        height = 30;
    }

    float getValue() const {
        return minValue + value * (maxValue - minValue);
    }

    void setValue(float v) {
        value = (v - minValue) / (maxValue - minValue);
        value = max(0.0f, min(1.0f, value));
    }

    // 外部からスクロールイベントを受け取る
    void handleScroll(float dx, float dy) {
        (void)dx;  // 未使用
        float delta = dy * 0.05f;
        float oldValue = value;
        value = max(0.0f, min(1.0f, value + delta));
        if (value != oldValue && onValueChanged) {
            onValueChanged(getValue());
        }
    }

    void draw() override {
        // 背景
        tc::setColor(0.2f, 0.2f, 0.25f);
        tc::fill();
        tc::drawRect(0, 0, width, height);

        // トラック
        float trackY = height / 2;
        float trackH = 4;
        tc::setColor(0.4f, 0.4f, 0.45f);
        tc::drawRect(0, trackY - trackH / 2, width, trackH);

        // ノブ
        float knobX = value * width;
        float knobW = 12;
        float knobH = height - 4;
        tc::setColor(isDragging_ ? tc::Color(0.6f, 0.7f, 0.9f) : tc::Color(0.5f, 0.6f, 0.8f));
        tc::drawRect(knobX - knobW / 2, 2, knobW, knobH);

        // ラベルと値
        tc::setColor(1.0f, 1.0f, 1.0f);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %.2f", label.c_str(), getValue());
        tc::drawBitmapString(buf, 4, -4, false);
    }

protected:
    bool isDragging_ = false;

    bool onMousePress(float lx, float ly, int btn) override {
        isDragging_ = true;
        updateValue(lx);
        return tc::RectNode::onMousePress(lx, ly, btn);
    }

    bool onMouseRelease(float lx, float ly, int btn) override {
        isDragging_ = false;
        return tc::RectNode::onMouseRelease(lx, ly, btn);
    }

    bool onMouseDrag(float lx, float ly, int btn) override {
        if (isDragging_) {
            updateValue(lx);
        }
        return tc::RectNode::onMouseDrag(lx, ly, btn);
    }

    bool onMouseMove(float lx, float ly) override {
        // ドラッグ中なら値を更新
        if (isDragging_) {
            updateValue(lx);
        }
        return tc::RectNode::onMouseMove(lx, ly);
    }

    bool onMouseScroll(float lx, float ly, float sx, float sy) override {
        // スクロールで値を変更
        float delta = sy * 0.05f;
        float oldValue = value;
        value = max(0.0f, min(1.0f, value + delta));
        if (value != oldValue && onValueChanged) {
            onValueChanged(getValue());
        }
        return tc::RectNode::onMouseScroll(lx, ly, sx, sy);
    }

private:
    void updateValue(float lx) {
        float oldValue = value;
        value = max(0.0f, min(1.0f, lx / width));
        if (value != oldValue && onValueChanged) {
            onValueChanged(getValue());
        }
    }
};

// =============================================================================
// UIScrollBox - スクロールで内容を移動するボックス
// =============================================================================
class UIScrollBox : public tc::RectNode {
public:
    using Ptr = shared_ptr<UIScrollBox>;

    float scrollY = 0;
    float contentHeight = 300;  // 内部コンテンツの高さ

    UIScrollBox() {
        enableEvents();
        width = 200;
        height = 150;
    }

    // 外部からスクロールイベントを受け取る
    void handleScroll(float dx, float dy) {
        (void)dx;  // 未使用
        float maxScroll = max(0.0f, contentHeight - height);
        scrollY = max(0.0f, min(maxScroll, scrollY - dy * 20));
    }

    void draw() override {
        // 背景
        tc::setColor(0.15f, 0.15f, 0.18f);
        tc::fill();
        tc::drawRect(0, 0, width, height);

        // クリッピングを設定（グローバル座標で、DPIスケール考慮）
        float gx, gy;
        localToGlobal(0, 0, gx, gy);
        float dpi = sapp_dpi_scale();
        tc::pushScissor(gx * dpi, gy * dpi, width * dpi, height * dpi);

        // スクロール可能なコンテンツ
        tc::pushMatrix();
        tc::translate(0, -scrollY);

        // コンテンツ（複数のアイテム）- クリッピングにより範囲外は自動的に非表示
        for (int i = 0; i < 10; i++) {
            float itemY = i * 30;
            tc::setColor(0.3f + i * 0.05f, 0.3f, 0.35f);
            tc::fill();
            tc::drawRect(5, itemY + 2, width - 10, 26);

            tc::setColor(1.0f, 1.0f, 1.0f);
            char buf[32];
            snprintf(buf, sizeof(buf), "Item %d", i + 1);
            tc::drawBitmapString(buf, 10, itemY + 18, false);
        }

        tc::popMatrix();

        // クリッピングを復元
        tc::popScissor();

        // 枠線
        tc::noFill();
        tc::stroke();
        tc::setColor(0.4f, 0.4f, 0.5f);
        tc::drawRect(0, 0, width, height);

        // スクロールバー
        float maxScroll = max(0.0f, contentHeight - height);
        if (maxScroll > 0) {
            float barHeight = height * (height / contentHeight);
            float barY = (scrollY / maxScroll) * (height - barHeight);
            tc::fill();
            tc::setColor(0.5f, 0.5f, 0.6f);
            tc::drawRect(width - 8, barY, 6, barHeight);
        }
    }

protected:
    bool onMouseScroll(float lx, float ly, float sx, float sy) override {
        float maxScroll = max(0.0f, contentHeight - height);
        scrollY = max(0.0f, min(maxScroll, scrollY - sy * 20));
        return tc::RectNode::onMouseScroll(lx, ly, sx, sy);
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
    void mouseReleased(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float dx, float dy) override;

private:
    UIButton::Ptr button1_;
    UIButton::Ptr button2_;
    UISlider::Ptr slider1_;
    UISlider::Ptr slider2_;
    UIScrollBox::Ptr scrollBox_;

    int clickCount_ = 0;
    tc::Color bgColor_ = tc::Color(0.1f, 0.1f, 0.12f);
};
