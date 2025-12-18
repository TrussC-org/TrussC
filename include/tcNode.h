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

// ホバー状態のキャッシュ（毎フレーム1回だけ更新）
namespace internal {
    inline Node* hoveredNode = nullptr;      // 現在ホバー中のノード
    inline Node* prevHoveredNode = nullptr;  // 前フレームのホバーノード
}

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
    // keepGlobalPosition: true の場合、子のグローバル座標を維持する
    void addChild(Ptr child, bool keepGlobalPosition = false) {
        if (!child || child.get() == this) return;

        // グローバル座標を保持する場合、移動前の位置を記録
        float globalX = 0, globalY = 0;
        if (keepGlobalPosition) {
            child->localToGlobal(0, 0, globalX, globalY);
        }

        // 既存の親から削除
        if (auto oldParent = child->parent_.lock()) {
            oldParent->removeChild(child);
        }

        child->parent_ = weak_from_this();
        children_.push_back(child);

        // グローバル座標を保持する場合、新しい親基準でローカル座標を再計算
        if (keepGlobalPosition) {
            float localX, localY;
            globalToLocal(globalX, globalY, localX, localY);
            child->x = localX;
            child->y = localY;
        }
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

    // マウスがこのノードの上にあるか（毎フレーム自動更新、O(1)）
    bool isMouseOver() const { return internal::hoveredNode == this; }

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

    // このノードのローカル変換行列を取得
    Mat4 getLocalMatrix() const {
        Mat4 mat = Mat4::translate(x, y, 0.0f);
        if (rotation != 0.0f) {
            mat = mat * Mat4::rotateZ(rotation);
        }
        if (scaleX != 1.0f || scaleY != 1.0f) {
            mat = mat * Mat4::scale(scaleX, scaleY, 1.0f);
        }
        return mat;
    }

    // このノードのグローバル変換行列を取得（親の変換を含む）
    Mat4 getGlobalMatrix() const {
        Mat4 local = getLocalMatrix();
        if (auto p = parent_.lock()) {
            return p->getGlobalMatrix() * local;
        }
        return local;
    }

    // グローバル変換行列の逆行列を取得
    Mat4 getGlobalMatrixInverse() const {
        return getGlobalMatrix().inverted();
    }

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
    virtual void drawTree() {
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

    // -------------------------------------------------------------------------
    // Ray-based Hit Test（イベントディスパッチ用）
    // -------------------------------------------------------------------------

    // ヒットテスト結果
    struct HitResult {
        Ptr node;           // ヒットしたノード
        float distance;     // Ray の origin からの距離
        Vec3 localPoint;    // ローカル座標でのヒット位置

        bool hit() const { return node != nullptr; }
    };

    // Global Ray でツリー全体をヒットテストし、最も手前のノードを返す
    // 描画順の逆（後から描画されたものが優先）で走査
    HitResult findHitNode(const Ray& globalRay) {
        return findHitNodeRecursive(globalRay, getGlobalMatrixInverse());
    }

    // マウスイベントをツリーに配信（2D モード用）
    // screenX, screenY: スクリーン座標
    // return: イベントを処理したノード（処理されなかった場合は nullptr）
    Ptr dispatchMousePress(float screenX, float screenY, int button) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            // ローカル座標を計算
            float localX = result.localPoint.x;
            float localY = result.localPoint.y;

            // イベントを配信
            if (result.node->onMousePress(localX, localY, button)) {
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseRelease(float screenX, float screenY, int button) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            float localX = result.localPoint.x;
            float localY = result.localPoint.y;

            if (result.node->onMouseRelease(localX, localY, button)) {
                return result.node;
            }
        }

        return nullptr;
    }

    Ptr dispatchMouseMove(float screenX, float screenY) {
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);

        if (result.hit()) {
            float localX = result.localPoint.x;
            float localY = result.localPoint.y;

            if (result.node->onMouseMove(localX, localY)) {
                return result.node;
            }
        }

        return nullptr;
    }

    // -------------------------------------------------------------------------
    // キーイベントのディスパッチ（全アクティブノードに broadcast）
    // -------------------------------------------------------------------------

    // キー押下を全ノードに配信
    bool dispatchKeyPress(int key) {
        return dispatchKeyPressRecursive(key);
    }

    // キー離しを全ノードに配信
    bool dispatchKeyRelease(int key) {
        return dispatchKeyReleaseRecursive(key);
    }

    // -------------------------------------------------------------------------
    // ホバー状態の更新（毎フレーム1回呼ぶ）
    // -------------------------------------------------------------------------

    void updateHoverState(float screenX, float screenY) {
        // 前フレームのホバーノードを保存
        internal::prevHoveredNode = internal::hoveredNode;

        // 新しいホバーノードを検索
        Ray globalRay = Ray::fromScreenPoint2D(screenX, screenY);
        HitResult result = findHitNode(globalRay);
        internal::hoveredNode = result.hit() ? result.node.get() : nullptr;

        // Enter/Leave イベントを発火
        if (internal::prevHoveredNode != internal::hoveredNode) {
            if (internal::prevHoveredNode) {
                internal::prevHoveredNode->onMouseLeave();
            }
            if (internal::hoveredNode) {
                internal::hoveredNode->onMouseEnter();
            }
        }
    }

private:
    // キーイベントの再帰的な配信
    bool dispatchKeyPressRecursive(int key) {
        if (!isActive) return false;

        // 自分が処理
        if (onKeyPress(key)) {
            return true;  // 消費された
        }

        // 子ノードに配信
        for (auto& child : children_) {
            if (child->dispatchKeyPressRecursive(key)) {
                return true;
            }
        }

        return false;
    }

    bool dispatchKeyReleaseRecursive(int key) {
        if (!isActive) return false;

        if (onKeyRelease(key)) {
            return true;
        }

        for (auto& child : children_) {
            if (child->dispatchKeyReleaseRecursive(key)) {
                return true;
            }
        }

        return false;
    }


    // 再帰的なヒットテスト（描画順の逆で走査）
    HitResult findHitNodeRecursive(const Ray& globalRay, const Mat4& parentInverseMatrix) {
        if (!isActive) return HitResult{};

        // このノードの逆行列を計算
        Mat4 localInverse = getLocalMatrix().inverted();
        Mat4 globalInverse = localInverse * parentInverseMatrix;

        // Global Ray を Local Ray に変換
        Ray localRay = globalRay.transformed(globalInverse);

        HitResult bestResult{};

        // 子ノードを後ろから走査（描画順の逆）
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            HitResult childResult = (*it)->findHitNodeRecursive(globalRay, globalInverse);
            if (childResult.hit()) {
                // 子の結果を採用（描画順で後 = 手前）
                bestResult = childResult;
                break;  // 最初にヒットしたものを優先（描画順で最後）
            }
        }

        // 子にヒットしなかった場合、自分をチェック
        if (!bestResult.hit()) {
            float distance;
            if (hitTest(localRay, distance)) {
                bestResult.node = std::dynamic_pointer_cast<Node>(shared_from_this());
                bestResult.distance = distance;
                bestResult.localPoint = localRay.at(distance);
            }
        }

        return bestResult;
    }

protected:

    // -------------------------------------------------------------------------
    // イベント（オーバーライド可能）
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // ヒットテスト（Ray ベース - 2D/3D 統一方式）
    // -------------------------------------------------------------------------

    // Ray によるヒットテスト（ローカル空間での判定）
    // localRay: このノードのローカル座標系に変換済みの Ray
    // outDistance: ヒットした場合の距離（Ray の origin からの t 値）
    // return: ヒットした場合 true
    virtual bool hitTest(const Ray& localRay, float& outDistance) {
        (void)localRay;
        (void)outDistance;
        return false;
    }

    // 従来の2D用ヒットテスト（後方互換性のため残す）
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

    virtual bool onMouseScroll(float localX, float localY, float scrollX, float scrollY) {
        (void)localX;
        (void)localY;
        (void)scrollX;
        (void)scrollY;
        return false;
    }

    // キーイベント（全ノードに broadcast される）
    virtual bool onKeyPress(int key) {
        (void)key;
        return false;
    }

    virtual bool onKeyRelease(int key) {
        (void)key;
        return false;
    }

    // マウス Enter/Leave（ホバー状態が変わった時に呼ばれる）
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}

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
