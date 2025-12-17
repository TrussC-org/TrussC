// =============================================================================
// tcxBox2dBody.h - Box2D ボディ基底クラス
// =============================================================================

#pragma once

#include "tcxBox2dWorld.h"
#include <tcNode.h>
#include <box2d/box2d.h>

namespace tcx::box2d {

// =============================================================================
// ボディ基底クラス
// =============================================================================
// 円、矩形、ポリゴンなど全ての物理オブジェクトの基底クラス
// tc::Node を継承し、シーングラフに統合可能
// =============================================================================
class Body : public tc::Node {
public:
    Body() = default;

    virtual ~Body() {
        destroy();
    }

    // コピー禁止
    Body(const Body&) = delete;
    Body& operator=(const Body&) = delete;

    // ムーブは許可
    Body(Body&& other) noexcept
        : tc::Node()
        , world_(other.world_)
        , body_(other.body_)
    {
        other.world_ = nullptr;
        other.body_ = nullptr;
    }

    Body& operator=(Body&& other) noexcept {
        if (this != &other) {
            destroy();
            world_ = other.world_;
            body_ = other.body_;
            other.world_ = nullptr;
            other.body_ = nullptr;
        }
        return *this;
    }

    // ---------------------------------------------------------------------
    // Node 統合: update で Box2D 座標を Node に同期
    // ---------------------------------------------------------------------
    void update() override {
        if (body_) {
            tc::Vec2 pos = World::toPixels(body_->GetPosition());
            x = pos.x;
            y = pos.y;
            rotation = body_->GetAngle();
        }
    }

    // ---------------------------------------------------------------------
    // 状態確認
    // ---------------------------------------------------------------------
    bool isCreated() const { return body_ != nullptr; }

    // ---------------------------------------------------------------------
    // 当たり判定（点がボディ内にあるか）
    // ---------------------------------------------------------------------
    bool containsPoint(const tc::Vec2& point) const {
        return containsPoint(point.x, point.y);
    }

    bool containsPoint(float px, float py) const {
        if (!body_) return false;
        b2Vec2 p = World::toBox2d(px, py);
        for (b2Fixture* f = body_->GetFixtureList(); f; f = f->GetNext()) {
            if (f->TestPoint(p)) return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------
    // 位置・回転（Box2D から取得）
    // ---------------------------------------------------------------------
    tc::Vec2 getPhysicsPosition() const {
        if (!body_) return tc::Vec2(0, 0);
        return World::toPixels(body_->GetPosition());
    }

    float getPhysicsRotation() const {
        return body_ ? body_->GetAngle() : 0;
    }

    float getPhysicsRotationDeg() const {
        return tc::degrees(getPhysicsRotation());
    }

    // ---------------------------------------------------------------------
    // 位置・回転の設定（Box2D に反映）
    // ---------------------------------------------------------------------
    void setPhysicsPosition(const tc::Vec2& pos) {
        setPhysicsPosition(pos.x, pos.y);
    }

    void setPhysicsPosition(float px, float py) {
        if (body_) {
            body_->SetTransform(World::toBox2d(px, py), body_->GetAngle());
        }
    }

    void setPhysicsRotation(float radians) {
        if (body_) {
            body_->SetTransform(body_->GetPosition(), radians);
        }
    }

    void setPhysicsRotationDeg(float deg) {
        setPhysicsRotation(tc::radians(deg));
    }

    void setPhysicsTransform(const tc::Vec2& pos, float radians) {
        if (body_) {
            body_->SetTransform(World::toBox2d(pos), radians);
        }
    }

    // ---------------------------------------------------------------------
    // 速度
    // ---------------------------------------------------------------------
    tc::Vec2 getVelocity() const {
        if (!body_) return tc::Vec2(0, 0);
        return World::toPixels(body_->GetLinearVelocity());
    }

    float getAngularVelocity() const {
        return body_ ? body_->GetAngularVelocity() : 0;
    }

    void setVelocity(const tc::Vec2& vel) {
        setVelocity(vel.x, vel.y);
    }

    void setVelocity(float vx, float vy) {
        if (body_) {
            body_->SetLinearVelocity(World::toBox2d(vx, vy));
        }
    }

    void setAngularVelocity(float omega) {
        if (body_) {
            body_->SetAngularVelocity(omega);
        }
    }

    // ---------------------------------------------------------------------
    // 力・インパルス
    // ---------------------------------------------------------------------

    void addForce(const tc::Vec2& force) {
        addForce(force.x, force.y);
    }

    void addForce(float fx, float fy) {
        if (body_) {
            body_->ApplyForceToCenter(World::toBox2d(fx, fy), true);
        }
    }

    void addForceAtPoint(const tc::Vec2& force, const tc::Vec2& point) {
        if (body_) {
            body_->ApplyForce(World::toBox2d(force), World::toBox2d(point), true);
        }
    }

    void addImpulse(const tc::Vec2& impulse) {
        addImpulse(impulse.x, impulse.y);
    }

    void addImpulse(float ix, float iy) {
        if (body_) {
            body_->ApplyLinearImpulseToCenter(World::toBox2d(ix, iy), true);
        }
    }

    void addImpulseAtPoint(const tc::Vec2& impulse, const tc::Vec2& point) {
        if (body_) {
            body_->ApplyLinearImpulse(World::toBox2d(impulse), World::toBox2d(point), true);
        }
    }

    void addTorque(float torque) {
        if (body_) {
            body_->ApplyTorque(torque, true);
        }
    }

    void addAngularImpulse(float impulse) {
        if (body_) {
            body_->ApplyAngularImpulse(impulse, true);
        }
    }

    // ---------------------------------------------------------------------
    // 物理パラメータ
    // ---------------------------------------------------------------------
    void setDensity(float density) {
        if (body_) {
            for (b2Fixture* f = body_->GetFixtureList(); f; f = f->GetNext()) {
                f->SetDensity(density);
            }
            body_->ResetMassData();
        }
    }

    void setFriction(float friction) {
        if (body_) {
            for (b2Fixture* f = body_->GetFixtureList(); f; f = f->GetNext()) {
                f->SetFriction(friction);
            }
        }
    }

    void setRestitution(float restitution) {
        if (body_) {
            for (b2Fixture* f = body_->GetFixtureList(); f; f = f->GetNext()) {
                f->SetRestitution(restitution);
            }
        }
    }

    float getDensity() const {
        if (body_ && body_->GetFixtureList()) {
            return body_->GetFixtureList()->GetDensity();
        }
        return 0;
    }

    float getFriction() const {
        if (body_ && body_->GetFixtureList()) {
            return body_->GetFixtureList()->GetFriction();
        }
        return 0;
    }

    float getRestitution() const {
        if (body_ && body_->GetFixtureList()) {
            return body_->GetFixtureList()->GetRestitution();
        }
        return 0;
    }

    float getMass() const {
        return body_ ? body_->GetMass() : 0;
    }

    // ---------------------------------------------------------------------
    // ボディタイプ
    // ---------------------------------------------------------------------
    void setStatic() {
        if (body_) body_->SetType(b2_staticBody);
    }

    void setDynamic() {
        if (body_) body_->SetType(b2_dynamicBody);
    }

    void setKinematic() {
        if (body_) body_->SetType(b2_kinematicBody);
    }

    bool isStaticBody() const {
        return body_ && body_->GetType() == b2_staticBody;
    }

    bool isDynamicBody() const {
        return body_ && body_->GetType() == b2_dynamicBody;
    }

    bool isKinematicBody() const {
        return body_ && body_->GetType() == b2_kinematicBody;
    }

    // ---------------------------------------------------------------------
    // その他の設定
    // ---------------------------------------------------------------------
    void setFixedRotation(bool fixed) {
        if (body_) body_->SetFixedRotation(fixed);
    }

    void setBullet(bool bullet) {
        if (body_) body_->SetBullet(bullet);
    }

    void setSensor(bool sensor) {
        if (body_) {
            for (b2Fixture* f = body_->GetFixtureList(); f; f = f->GetNext()) {
                f->SetSensor(sensor);
            }
        }
    }

    void setAwake(bool awake) {
        if (body_) body_->SetAwake(awake);
    }

    bool isAwake() const {
        return body_ && body_->IsAwake();
    }

    void setEnabled(bool enabled) {
        if (body_) body_->SetEnabled(enabled);
    }

    bool isBodyEnabled() const {
        return body_ && body_->IsEnabled();
    }

    // ---------------------------------------------------------------------
    // 削除
    // ---------------------------------------------------------------------
    void destroy() {
        if (body_ && world_ && world_->getWorld()) {
            world_->getWorld()->DestroyBody(body_);
            body_ = nullptr;
        }
    }

    // ---------------------------------------------------------------------
    // Box2Dへの直接アクセス
    // ---------------------------------------------------------------------
    b2Body* getBody() { return body_; }
    const b2Body* getBody() const { return body_; }

protected:
    void setBody(b2Body* body) { body_ = body; }

    b2Fixture* getFixture() {
        return body_ ? body_->GetFixtureList() : nullptr;
    }

    const b2Fixture* getFixture() const {
        return body_ ? body_->GetFixtureList() : nullptr;
    }

    World* world_ = nullptr;
    b2Body* body_ = nullptr;
};

} // namespace tcx::box2d
