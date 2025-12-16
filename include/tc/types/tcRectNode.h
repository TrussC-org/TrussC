#pragma once

#include "tcNode.h"

namespace trussc {

// =============================================================================
// RectNode - 矩形を持つ2D UIノード
// Ray-based Hit Test により、3D空間内で回転・スケールしても正確に判定可能
// =============================================================================

class RectNode : public Node {
public:
    using Ptr = std::shared_ptr<RectNode>;
    using WeakPtr = std::weak_ptr<RectNode>;

    // -------------------------------------------------------------------------
    // イベント（外部からリスナー登録可能）
    // -------------------------------------------------------------------------
    Event<MouseEventArgs> mousePressed;
    Event<MouseEventArgs> mouseReleased;
    Event<MouseDragEventArgs> mouseDragged;
    Event<ScrollEventArgs> mouseScrolled;

    // サイズ（ローカル座標系での幅・高さ）
    float width = 100.0f;
    float height = 100.0f;

    // -------------------------------------------------------------------------
    // サイズ設定
    // -------------------------------------------------------------------------

    void setSize(float w, float h) {
        width = w;
        height = h;
    }

    void setSize(float size) {
        width = height = size;
    }

    Vec2 getSize() const {
        return Vec2(width, height);
    }

    // -------------------------------------------------------------------------
    // 矩形取得（ローカル座標系）
    // -------------------------------------------------------------------------

    // 原点 (0,0) から始まる矩形として扱う
    // 中心を原点にしたい場合は、描画時に -width/2, -height/2 オフセットする
    float getLeft() const { return 0; }
    float getRight() const { return width; }
    float getTop() const { return 0; }
    float getBottom() const { return height; }

    // -------------------------------------------------------------------------
    // Ray-based Hit Test
    // -------------------------------------------------------------------------

    // Ray によるヒットテスト（Z=0 平面との交差判定）
    // ローカル空間では、このノードは Z=0 平面上に width x height の矩形として存在する
    bool hitTest(const Ray& localRay, float& outDistance) override {
        // イベントが有効でない場合はヒットしない
        if (!isEventsEnabled()) {
            return false;
        }

        // Z=0 平面との交差を計算
        float t;
        Vec3 hitPoint;
        if (!localRay.intersectZPlane(t, hitPoint)) {
            return false;
        }

        // 交点が矩形内にあるかチェック
        if (hitPoint.x >= 0 && hitPoint.x <= width &&
            hitPoint.y >= 0 && hitPoint.y <= height) {
            outDistance = t;
            return true;
        }

        return false;
    }

    // 従来の2D用ヒットテスト（後方互換性）
    bool hitTest(float localX, float localY) override {
        if (!isEventsEnabled()) {
            return false;
        }
        return localX >= 0 && localX <= width &&
               localY >= 0 && localY <= height;
    }

    // -------------------------------------------------------------------------
    // 描画ヘルパー（オーバーライド用）
    // -------------------------------------------------------------------------

    // 矩形を描画（派生クラスでオーバーライド可能）
    void draw() override {
        // デフォルトでは何も描画しない
        // 派生クラスで drawRect(0, 0, width, height) などを呼ぶ
    }

protected:
    // -------------------------------------------------------------------------
    // マウスイベント（イベントを発火）
    // -------------------------------------------------------------------------

    bool onMousePress(float localX, float localY, int button) override {
        MouseEventArgs args;
        args.x = localX;
        args.y = localY;
        args.button = button;
        mousePressed.notify(args);
        return true;  // イベントを消費
    }

    bool onMouseRelease(float localX, float localY, int button) override {
        MouseEventArgs args;
        args.x = localX;
        args.y = localY;
        args.button = button;
        mouseReleased.notify(args);
        return true;
    }

    bool onMouseDrag(float localX, float localY, int button) override {
        MouseDragEventArgs args;
        args.x = localX;
        args.y = localY;
        args.button = button;
        args.deltaX = localX - getMouseX();  // 簡易的な delta
        args.deltaY = localY - getMouseY();
        mouseDragged.notify(args);
        return true;
    }

    bool onMouseScroll(float localX, float localY, float scrollX, float scrollY) override {
        (void)localX;
        (void)localY;
        ScrollEventArgs args;
        args.scrollX = scrollX;
        args.scrollY = scrollY;
        mouseScrolled.notify(args);
        return true;
    }

    // -------------------------------------------------------------------------
    // 描画ヘルパー
    // -------------------------------------------------------------------------

    // 矩形を塗りつぶしで描画するヘルパー
    void drawRectFill() {
        fill();
        noStroke();
        drawRect(0, 0, width, height);
    }

    // 矩形を枠線で描画するヘルパー
    void drawRectStroke() {
        noFill();
        stroke();
        drawRect(0, 0, width, height);
    }

    // 矩形を塗りつぶし＋枠線で描画するヘルパー
    void drawRectFillStroke() {
        fill();
        stroke();
        drawRect(0, 0, width, height);
    }
};

// =============================================================================
// Button - シンプルなボタン（RectNode の例）
// =============================================================================

class Button : public RectNode {
public:
    using Ptr = std::shared_ptr<Button>;

    // 状態
    bool isHovered = false;
    bool isPressed = false;

    // 色設定
    Color normalColor = Color(0.3f, 0.3f, 0.3f);
    Color hoverColor = Color(0.4f, 0.4f, 0.5f);
    Color pressColor = Color(0.2f, 0.2f, 0.3f);

    // ラベル
    std::string label;

    Button() {
        enableEvents();  // イベントを有効化
    }

    void draw() override {
        // 状態に応じた色を設定
        if (isPressed) {
            setColor(pressColor);
        } else if (isHovered) {
            setColor(hoverColor);
        } else {
            setColor(normalColor);
        }

        drawRectFill();

        // ラベルを中央に描画
        if (!label.empty()) {
            setColor(1.0f, 1.0f, 1.0f);
            // 簡易的な中央揃え（フォントサイズを考慮していない簡易版）
            float textX = width / 2 - label.length() * 4;
            float textY = height / 2 + 4;
            drawBitmapString(label, textX, textY);
        }
    }

protected:
    bool onMousePress(float localX, float localY, int button) override {
        isPressed = true;
        return RectNode::onMousePress(localX, localY, button);  // 親のイベントも発火
    }

    bool onMouseRelease(float localX, float localY, int button) override {
        isPressed = false;
        return RectNode::onMouseRelease(localX, localY, button);
    }
};

} // namespace trussc
