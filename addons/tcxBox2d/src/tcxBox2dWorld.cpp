// =============================================================================
// tcxBox2dWorld.cpp - Box2D ワールド管理
// =============================================================================

#include "tcxBox2d/tcxBox2dWorld.h"
#include "tcxBox2d/tcxBox2dBody.h"

namespace tcx::box2d {

// デフォルトスケール: 30ピクセル = 1メートル
float World::scale = 30.0f;

// =============================================================================
// コンストラクタ / デストラクタ
// =============================================================================
World::World() = default;

World::~World() {
    clear();
}

World::World(World&& other) noexcept
    : world_(std::move(other.world_))
    , timeStep_(other.timeStep_)
    , velocityIterations_(other.velocityIterations_)
    , positionIterations_(other.positionIterations_)
    , groundBody_(other.groundBody_)
{
    other.groundBody_ = nullptr;
}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        clear();
        world_ = std::move(other.world_);
        timeStep_ = other.timeStep_;
        velocityIterations_ = other.velocityIterations_;
        positionIterations_ = other.positionIterations_;
        groundBody_ = other.groundBody_;
        other.groundBody_ = nullptr;
    }
    return *this;
}

// =============================================================================
// 初期化
// =============================================================================
void World::setup(const tc::Vec2& gravity) {
    setup(gravity.x, gravity.y);
}

void World::setup(float gravityX, float gravityY) {
    // Box2D座標系では下向きが正なので、そのまま使用
    b2Vec2 g(gravityX / scale, gravityY / scale);
    world_ = std::make_unique<b2World>(g);
}

// =============================================================================
// シミュレーション
// =============================================================================
void World::update() {
    if (world_) {
        world_->Step(timeStep_, velocityIterations_, positionIterations_);
    }
}

void World::setFPS(float fps) {
    if (fps > 0) {
        timeStep_ = 1.0f / fps;
    }
}

void World::setVelocityIterations(int n) {
    velocityIterations_ = n;
}

void World::setPositionIterations(int n) {
    positionIterations_ = n;
}

void World::setGravity(const tc::Vec2& gravity) {
    setGravity(gravity.x, gravity.y);
}

void World::setGravity(float x, float y) {
    if (world_) {
        world_->SetGravity(b2Vec2(x / scale, y / scale));
    }
}

tc::Vec2 World::getGravity() const {
    if (world_) {
        b2Vec2 g = world_->GetGravity();
        return tc::Vec2(g.x * scale, g.y * scale);
    }
    return tc::Vec2(0, 0);
}

// =============================================================================
// 境界
// =============================================================================
void World::createBounds(float x, float y, float width, float height) {
    if (!world_) return;

    // 既存の境界を削除
    if (groundBody_) {
        world_->DestroyBody(groundBody_);
        groundBody_ = nullptr;
    }

    // 静的ボディを作成
    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    groundBody_ = world_->CreateBody(&bodyDef);

    // 四辺を作成
    float halfW = toBox2d(width / 2);
    float halfH = toBox2d(height / 2);
    float centerX = toBox2d(x + width / 2);
    float centerY = toBox2d(y + height / 2);

    b2EdgeShape edge;

    // 下辺
    edge.SetTwoSided(b2Vec2(centerX - halfW, centerY + halfH),
                     b2Vec2(centerX + halfW, centerY + halfH));
    groundBody_->CreateFixture(&edge, 0.0f);

    // 上辺
    edge.SetTwoSided(b2Vec2(centerX - halfW, centerY - halfH),
                     b2Vec2(centerX + halfW, centerY - halfH));
    groundBody_->CreateFixture(&edge, 0.0f);

    // 左辺
    edge.SetTwoSided(b2Vec2(centerX - halfW, centerY - halfH),
                     b2Vec2(centerX - halfW, centerY + halfH));
    groundBody_->CreateFixture(&edge, 0.0f);

    // 右辺
    edge.SetTwoSided(b2Vec2(centerX + halfW, centerY - halfH),
                     b2Vec2(centerX + halfW, centerY + halfH));
    groundBody_->CreateFixture(&edge, 0.0f);
}

void World::createBounds() {
    createBounds(0, 0, tc::getWindowWidth(), tc::getWindowHeight());
}

void World::createGround(float y, float width) {
    if (!world_) return;

    if (groundBody_) {
        world_->DestroyBody(groundBody_);
        groundBody_ = nullptr;
    }

    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    groundBody_ = world_->CreateBody(&bodyDef);

    b2EdgeShape edge;
    edge.SetTwoSided(b2Vec2(0, toBox2d(y)),
                     b2Vec2(toBox2d(width), toBox2d(y)));
    groundBody_->CreateFixture(&edge, 0.0f);
}

void World::createGround() {
    createGround(tc::getWindowHeight(), tc::getWindowWidth());
}

// =============================================================================
// ボディ管理
// =============================================================================
void World::clear() {
    if (world_) {
        // マウスジョイントを破棄
        endDrag();

        // 全ボディを削除
        b2Body* body = world_->GetBodyList();
        while (body) {
            b2Body* next = body->GetNext();
            world_->DestroyBody(body);
            body = next;
        }
        groundBody_ = nullptr;
        dragAnchorBody_ = nullptr;
    }
}

int World::getBodyCount() const {
    return world_ ? world_->GetBodyCount() : 0;
}

// =============================================================================
// ポイントクエリ
// =============================================================================
Body* World::getBodyAtPoint(const tc::Vec2& point) {
    return getBodyAtPoint(point.x, point.y);
}

Body* World::getBodyAtPoint(float px, float py) {
    if (!world_) return nullptr;

    b2Vec2 point = toBox2d(px, py);

    // 全ボディをチェック（静的ボディは除外）
    for (b2Body* b = world_->GetBodyList(); b; b = b->GetNext()) {
        if (b->GetType() != b2_dynamicBody) continue;

        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext()) {
            if (f->TestPoint(point)) {
                // UserData から Body* を取得
                uintptr_t ptr = b->GetUserData().pointer;
                if (ptr != 0) {
                    return reinterpret_cast<Body*>(ptr);
                }
            }
        }
    }
    return nullptr;
}

// =============================================================================
// マウスドラッグ
// =============================================================================
void World::startDrag(Body* body, const tc::Vec2& target) {
    startDrag(body, target.x, target.y);
}

void World::startDrag(Body* body, float tx, float ty) {
    if (!world_ || !body || !body->getBody()) return;

    // 既存のジョイントがあれば破棄
    endDrag();

    // アンカー用の静的ボディを作成（まだなければ）
    if (!dragAnchorBody_) {
        b2BodyDef anchorDef;
        anchorDef.type = b2_staticBody;
        dragAnchorBody_ = world_->CreateBody(&anchorDef);
    }

    // マウスジョイントを作成
    b2MouseJointDef jointDef;
    jointDef.bodyA = dragAnchorBody_;
    jointDef.bodyB = body->getBody();
    jointDef.target = toBox2d(tx, ty);
    jointDef.maxForce = 1000.0f * body->getBody()->GetMass();
    jointDef.stiffness = 50.0f;
    jointDef.damping = 0.9f;

    mouseJoint_ = static_cast<b2MouseJoint*>(world_->CreateJoint(&jointDef));

    // ドラッグ中はボディを起こす
    body->getBody()->SetAwake(true);
}

void World::updateDrag(const tc::Vec2& target) {
    updateDrag(target.x, target.y);
}

void World::updateDrag(float tx, float ty) {
    if (mouseJoint_) {
        mouseJoint_->SetTarget(toBox2d(tx, ty));
    }
}

void World::endDrag() {
    if (mouseJoint_ && world_) {
        world_->DestroyJoint(mouseJoint_);
        mouseJoint_ = nullptr;
    }
}

bool World::isDragging() const {
    return mouseJoint_ != nullptr;
}

tc::Vec2 World::getDragAnchor() const {
    if (mouseJoint_) {
        return toPixels(mouseJoint_->GetAnchorB());
    }
    return tc::Vec2(0, 0);
}

// =============================================================================
// 座標変換
// =============================================================================
b2Vec2 World::toBox2d(const tc::Vec2& v) {
    return b2Vec2(v.x / scale, v.y / scale);
}

b2Vec2 World::toBox2d(float x, float y) {
    return b2Vec2(x / scale, y / scale);
}

float World::toBox2d(float val) {
    return val / scale;
}

tc::Vec2 World::toPixels(const b2Vec2& v) {
    return tc::Vec2(v.x * scale, v.y * scale);
}

float World::toPixels(float val) {
    return val * scale;
}

} // namespace tcx::box2d
