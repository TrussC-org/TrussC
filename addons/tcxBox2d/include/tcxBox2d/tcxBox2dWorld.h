// =============================================================================
// tcxBox2dWorld.h - Box2D ワールド管理
// =============================================================================

#pragma once

#include <TrussC.h>
#include <box2d/box2d.h>
#include <memory>
#include <vector>

namespace tcx::box2d {

// 前方宣言
class Body;

// =============================================================================
// Box2D ワールド
// =============================================================================
// 物理シミュレーションを管理するメインクラス
// =============================================================================
class World {
public:
    // ピクセル/メートル変換スケール（デフォルト: 30px = 1m）
    static float scale;

    World();
    ~World();

    // コピー禁止
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ムーブは許可
    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

    // ---------------------------------------------------------------------
    // 初期化
    // ---------------------------------------------------------------------

    // 重力を設定してワールドを初期化
    void setup(const tc::Vec2& gravity = tc::Vec2(0, 10));
    void setup(float gravityX, float gravityY);

    // ---------------------------------------------------------------------
    // シミュレーション
    // ---------------------------------------------------------------------

    // 物理シミュレーションを1ステップ進める
    void update();

    // シミュレーションパラメータ
    void setFPS(float fps);               // FPS（デフォルト: 60）
    void setVelocityIterations(int n);    // 速度計算反復回数（デフォルト: 8）
    void setPositionIterations(int n);    // 位置計算反復回数（デフォルト: 3）

    // 重力
    void setGravity(const tc::Vec2& gravity);
    void setGravity(float x, float y);
    tc::Vec2 getGravity() const;

    // ---------------------------------------------------------------------
    // 境界（画面端の壁）
    // ---------------------------------------------------------------------

    // 画面端に壁を作成
    void createBounds(float x, float y, float width, float height);
    void createBounds();  // 現在のウィンドウサイズで作成

    // 地面だけ作成（画面下端）
    void createGround(float y, float width);
    void createGround();  // 現在のウィンドウサイズで作成

    // ---------------------------------------------------------------------
    // ボディ管理
    // ---------------------------------------------------------------------

    // 登録されている全ボディを削除
    void clear();

    // ボディ数
    int getBodyCount() const;

    // ---------------------------------------------------------------------
    // ポイントクエリ（指定位置のボディを取得）
    // ---------------------------------------------------------------------
    Body* getBodyAtPoint(const tc::Vec2& point);
    Body* getBodyAtPoint(float x, float y);

    // ---------------------------------------------------------------------
    // マウスドラッグ（b2MouseJoint を使用）
    // ---------------------------------------------------------------------

    // ドラッグ開始（ボディと開始位置を指定）
    void startDrag(Body* body, const tc::Vec2& target);
    void startDrag(Body* body, float x, float y);

    // ドラッグ中（マウス位置を更新）
    void updateDrag(const tc::Vec2& target);
    void updateDrag(float x, float y);

    // ドラッグ終了
    void endDrag();

    // ドラッグ中かどうか
    bool isDragging() const;

    // ドラッグ中のアンカー位置（ボディ側の接続点）を取得
    tc::Vec2 getDragAnchor() const;

    // ---------------------------------------------------------------------
    // 座標変換
    // ---------------------------------------------------------------------

    // ピクセル座標 → Box2D座標
    static b2Vec2 toBox2d(const tc::Vec2& v);
    static b2Vec2 toBox2d(float x, float y);
    static float toBox2d(float val);

    // Box2D座標 → ピクセル座標
    static tc::Vec2 toPixels(const b2Vec2& v);
    static float toPixels(float val);

    // ---------------------------------------------------------------------
    // Box2Dへの直接アクセス
    // ---------------------------------------------------------------------
    b2World* getWorld() { return world_.get(); }
    const b2World* getWorld() const { return world_.get(); }

private:
    std::unique_ptr<b2World> world_;

    // シミュレーションパラメータ
    float timeStep_ = 1.0f / 60.0f;
    int velocityIterations_ = 8;
    int positionIterations_ = 3;

    // 境界用ボディ
    b2Body* groundBody_ = nullptr;

    // マウスドラッグ用
    b2MouseJoint* mouseJoint_ = nullptr;
    b2Body* dragAnchorBody_ = nullptr;  // ジョイントのアンカー用静的ボディ
};

} // namespace tcx::box2d
