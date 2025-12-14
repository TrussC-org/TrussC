#pragma once

#include "TrussC.h"
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdint>

// =============================================================================
// trussc 名前空間
// =============================================================================
namespace trussc {

// 前方宣言
class Node;
using NodePtr = std::shared_ptr<Node>;
using NodeWeakPtr = std::weak_ptr<Node>;

// =============================================================================
// Node - シーングラフの基底クラス
// すべてのノードはこのクラスを継承する
// =============================================================================

class Node : public std::enable_shared_from_this<Node> {
public:
    using Ptr = std::shared_ptr<Node>;
    using WeakPtr = std::weak_ptr<Node>;

    virtual ~Node() = default;

    // -------------------------------------------------------------------------
    // ライフサイクル（オーバーライド可能）
    // -------------------------------------------------------------------------

    virtual void setup() {}
    virtual void update() {}
    virtual void draw() {}
    virtual void cleanup() {}

    // -------------------------------------------------------------------------
    // ツリー操作
    // -------------------------------------------------------------------------

    // 子ノードを追加
    void addChild(Ptr child) {
        if (!child || child.get() == this) return;

        // 既存の親から削除
        if (auto oldParent = child->parent_.lock()) {
            oldParent->removeChild(child);
        }

        child->parent_ = weak_from_this();
        children_.push_back(child);
    }

    // 子ノードを削除
    void removeChild(Ptr child) {
        if (!child) return;

        auto it = std::find(children_.begin(), children_.end(), child);
        if (it != children_.end()) {
            (*it)->parent_.reset();
            children_.erase(it);
        }
    }

    // すべての子ノードを削除
    void removeAllChildren() {
        for (auto& child : children_) {
            child->parent_.reset();
        }
        children_.clear();
    }

    // 親ノードを取得
    Ptr getParent() const {
        return parent_.lock();
    }

    // 子ノードのリストを取得
    const std::vector<Ptr>& getChildren() const {
        return children_;
    }

    // 子ノードの数を取得
    size_t getChildCount() const {
        return children_.size();
    }

    // -------------------------------------------------------------------------
    // 状態
    // -------------------------------------------------------------------------

    bool isActive = true;    // false: update/drawがスキップされる
    bool isVisible = true;   // false: drawのみスキップされる

    // イベント有効化（enableEvents() を呼んだノードのみがヒットテスト対象）
    void enableEvents() { eventsEnabled_ = true; }
    void disableEvents() { eventsEnabled_ = false; }
    bool isEventsEnabled() const { return eventsEnabled_; }

    // -------------------------------------------------------------------------
    // 変換（トランスフォーム）
    // -------------------------------------------------------------------------

    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;      // ラジアン
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    // 回転を度数法で設定
    void setRotationDeg(float degrees) {
        rotation = degrees * PI / 180.0f;
    }

    // 回転を度数法で取得
    float getRotationDeg() const {
        return rotation * 180.0f / PI;
    }

    // -------------------------------------------------------------------------
    // 座標変換
    // -------------------------------------------------------------------------

    // グローバル座標をこのノードのローカル座標に変換
    void globalToLocal(float globalX, float globalY, float& localX, float& localY) const {
        // まず親のローカル座標に変換
        float parentLocalX = globalX;
        float parentLocalY = globalY;
        if (auto p = parent_.lock()) {
            p->globalToLocal(globalX, globalY, parentLocalX, parentLocalY);
        }

        // 親のローカル座標からこのノードのローカル座標に変換
        // translate の逆
        float dx = parentLocalX - x;
        float dy = parentLocalY - y;

        // rotate の逆
        float cosR = std::cos(-rotation);
        float sinR = std::sin(-rotation);
        float rx = dx * cosR - dy * sinR;
        float ry = dx * sinR + dy * cosR;

        // scale の逆
        localX = (scaleX != 0.0f) ? rx / scaleX : rx;
        localY = (scaleY != 0.0f) ? ry / scaleY : ry;
    }

    // ローカル座標をグローバル座標に変換
    void localToGlobal(float localX, float localY, float& globalX, float& globalY) const {
        // scale を適用
        float sx = localX * scaleX;
        float sy = localY * scaleY;

        // rotate を適用
        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        float rx = sx * cosR - sy * sinR;
        float ry = sx * sinR + sy * cosR;

        // translate を適用
        float tx = rx + x;
        float ty = ry + y;

        // 親がいれば、親のローカル→グローバル変換を適用
        if (auto p = parent_.lock()) {
            p->localToGlobal(tx, ty, globalX, globalY);
        } else {
            globalX = tx;
            globalY = ty;
        }
    }

    // -------------------------------------------------------------------------
    // マウス座標（ローカル座標系）
    // -------------------------------------------------------------------------

    // このノードのローカル座標系でのマウスX座標
    float getMouseX() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalMouseX(), trussc::getGlobalMouseY(), lx, ly);
        return lx;
    }

    // このノードのローカル座標系でのマウスY座標
    float getMouseY() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalMouseX(), trussc::getGlobalMouseY(), lx, ly);
        return ly;
    }

    // 前フレームのマウスX座標（ローカル座標系）
    float getPMouseX() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalPMouseX(), trussc::getGlobalPMouseY(), lx, ly);
        return lx;
    }

    // 前フレームのマウスY座標（ローカル座標系）
    float getPMouseY() const {
        float lx, ly;
        globalToLocal(trussc::getGlobalPMouseX(), trussc::getGlobalPMouseY(), lx, ly);
        return ly;
    }

    // -------------------------------------------------------------------------
    // 再帰的な更新・描画（フレームワークが自動で呼ぶ）
    // -------------------------------------------------------------------------

    // 自分と子ノードを再帰的に更新
    void updateTree() {
        if (!isActive) return;

        processTimers();
        update();  // ユーザーコード

        // 子ノードを自動的に更新
        for (auto& child : children_) {
            child->updateTree();
        }
    }

    // 自分と子ノードを再帰的に描画
    void drawTree() {
        if (!isActive) return;

        pushMatrix();

        // 変換を適用
        translate(x, y);
        if (rotation != 0.0f) {
            rotate(rotation);
        }
        if (scaleX != 1.0f || scaleY != 1.0f) {
            scale(scaleX, scaleY);
        }

        // ユーザーの描画
        if (isVisible) {
            draw();
        }

        // 子ノードを自動的に描画
        for (auto& child : children_) {
            child->drawTree();
        }

        popMatrix();
    }

protected:

    // -------------------------------------------------------------------------
    // イベント（オーバーライド可能）
    // -------------------------------------------------------------------------

    // ヒットテスト（ローカル座標でのクリック判定）
    // trueを返すと、このノードがイベントを受け取る
    virtual bool hitTest(float localX, float localY) {
        (void)localX;
        (void)localY;
        return false;
    }

    // マウスイベント（ローカル座標で届く）
    // trueを返すと、イベントが消費される（親に伝播しない）
    virtual bool onMousePress(float localX, float localY, int button) {
        (void)localX;
        (void)localY;
        (void)button;
        return false;
    }

    virtual bool onMouseRelease(float localX, float localY, int button) {
        (void)localX;
        (void)localY;
        (void)button;
        return false;
    }

    virtual bool onMouseMove(float localX, float localY) {
        (void)localX;
        (void)localY;
        return false;
    }

    virtual bool onMouseDrag(float localX, float localY, int button) {
        (void)localX;
        (void)localY;
        (void)button;
        return false;
    }

    // キーイベント
    virtual bool onKeyPress(int key) {
        (void)key;
        return false;
    }

    virtual bool onKeyRelease(int key) {
        (void)key;
        return false;
    }

    // -------------------------------------------------------------------------
    // タイマー
    // -------------------------------------------------------------------------

    // 指定秒数後に1回だけコールバックを実行
    uint64_t callAfter(double delay, std::function<void()> callback) {
        uint64_t id = nextTimerId_++;
        double triggerTime = getElapsedTime() + delay;
        timers_.push_back({id, triggerTime, 0.0, callback, false});
        return id;
    }

    // 指定間隔で繰り返しコールバックを実行
    uint64_t callEvery(double interval, std::function<void()> callback) {
        uint64_t id = nextTimerId_++;
        double triggerTime = getElapsedTime() + interval;
        timers_.push_back({id, triggerTime, interval, callback, true});
        return id;
    }

    // タイマーをキャンセル
    void cancelTimer(uint64_t id) {
        timers_.erase(
            std::remove_if(timers_.begin(), timers_.end(),
                [id](const Timer& t) { return t.id == id; }),
            timers_.end()
        );
    }

    // すべてのタイマーをキャンセル
    void cancelAllTimers() {
        timers_.clear();
    }

protected:
    WeakPtr parent_;
    std::vector<Ptr> children_;
    bool eventsEnabled_ = false;  // enableEvents() で有効化

    // タイマー構造体
    struct Timer {
        uint64_t id;
        double triggerTime;
        double interval;
        std::function<void()> callback;
        bool repeating;
    };

    std::vector<Timer> timers_;
    inline static uint64_t nextTimerId_ = 1;

    // タイマーを処理（updateRecursive内で呼ばれる）
    void processTimers() {
        double currentTime = getElapsedTime();
        std::vector<Timer> toRemove;

        for (auto& timer : timers_) {
            if (currentTime >= timer.triggerTime) {
                timer.callback();

                if (timer.repeating) {
                    timer.triggerTime = currentTime + timer.interval;
                } else {
                    toRemove.push_back(timer);
                }
            }
        }

        // 実行済みの非繰り返しタイマーを削除
        for (const auto& t : toRemove) {
            cancelTimer(t.id);
        }
    }
};

} // namespace trussc
