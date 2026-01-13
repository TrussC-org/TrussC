#pragma once

#include "tcNode.h"
#include "../animation/tcEasing.h"

namespace trussc {

// =============================================================================
// TweenMod - Node property animation
//
// Animates Node's position, scale, and rotation properties with easing.
// Multiple TweenMods can be added to the same node for different animations.
//
// Usage:
//   auto tween = node->addMod<TweenMod>();
//   tween->moveTo(100, 200).duration(0.5f).ease(EaseType::Cubic).start();
//
//   // Or chain multiple properties with same duration
//   tween->moveTo(100, 200).scaleTo(2.0f).duration(0.5f).start();
//
//   // With callback
//   tween->complete->addListener([](){ logNotice() << "done!"; });
//
// =============================================================================

class TweenMod : public Mod {
public:
    // Completion event - fired when all tweens finish
    Event<void> complete;

    TweenMod() = default;

    // -------------------------------------------------------------------------
    // Position (moveTo, moveBy)
    // -------------------------------------------------------------------------

    // Move to absolute position (from current)
    TweenMod& moveTo(float x, float y, float z = 0.0f) {
        posTarget_ = Vec3(x, y, z);
        posEnabled_ = true;
        posRelative_ = false;
        return *this;
    }
    TweenMod& moveTo(const Vec3& pos) {
        return moveTo(pos.x, pos.y, pos.z);
    }
    TweenMod& moveTo(const Vec2& pos) {
        return moveTo(pos.x, pos.y, 0.0f);
    }

    // Move by relative amount
    TweenMod& moveBy(float dx, float dy, float dz = 0.0f) {
        posTarget_ = Vec3(dx, dy, dz);
        posEnabled_ = true;
        posRelative_ = true;
        return *this;
    }
    TweenMod& moveBy(const Vec3& delta) {
        return moveBy(delta.x, delta.y, delta.z);
    }
    TweenMod& moveBy(const Vec2& delta) {
        return moveBy(delta.x, delta.y, 0.0f);
    }

    // Set explicit start position
    TweenMod& moveFrom(float x, float y, float z = 0.0f) {
        posFrom_ = Vec3(x, y, z);
        posFromSet_ = true;
        return *this;
    }
    TweenMod& moveFrom(const Vec3& pos) {
        return moveFrom(pos.x, pos.y, pos.z);
    }

    // -------------------------------------------------------------------------
    // Scale (scaleTo, scaleBy)
    // -------------------------------------------------------------------------

    // Scale to absolute value
    TweenMod& scaleTo(float uniform) {
        return scaleTo(uniform, uniform, uniform);
    }
    TweenMod& scaleTo(float sx, float sy, float sz = 1.0f) {
        scaleTarget_ = Vec3(sx, sy, sz);
        scaleEnabled_ = true;
        scaleRelative_ = false;
        return *this;
    }
    TweenMod& scaleTo(const Vec3& s) {
        return scaleTo(s.x, s.y, s.z);
    }

    // Scale by relative multiplier
    TweenMod& scaleBy(float factor) {
        return scaleBy(factor, factor, factor);
    }
    TweenMod& scaleBy(float sx, float sy, float sz = 1.0f) {
        scaleTarget_ = Vec3(sx, sy, sz);
        scaleEnabled_ = true;
        scaleRelative_ = true;
        return *this;
    }

    // Set explicit start scale
    TweenMod& scaleFrom(float uniform) {
        return scaleFrom(uniform, uniform, uniform);
    }
    TweenMod& scaleFrom(float sx, float sy, float sz = 1.0f) {
        scaleFrom_ = Vec3(sx, sy, sz);
        scaleFromSet_ = true;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Rotation 2D (rotateTo, rotateBy)
    // -------------------------------------------------------------------------

    // Rotate to absolute angle (Z-axis, radians)
    TweenMod& rotateTo(float radians) {
        rotTarget_ = radians;
        rotEnabled_ = true;
        rotRelative_ = false;
        return *this;
    }

    // Rotate by relative angle (Z-axis, radians)
    TweenMod& rotateBy(float radians) {
        rotTarget_ = radians;
        rotEnabled_ = true;
        rotRelative_ = true;
        return *this;
    }

    // Set explicit start rotation
    TweenMod& rotateFrom(float radians) {
        rotFrom_ = radians;
        rotFromSet_ = true;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Rotation 3D (Euler angles)
    // -------------------------------------------------------------------------

    // Rotate X axis to angle
    TweenMod& rotateXTo(float radians) {
        eulerTarget_.x = radians;
        eulerXEnabled_ = true;
        eulerXRelative_ = false;
        return *this;
    }
    TweenMod& rotateXBy(float radians) {
        eulerTarget_.x = radians;
        eulerXEnabled_ = true;
        eulerXRelative_ = true;
        return *this;
    }

    // Rotate Y axis to angle
    TweenMod& rotateYTo(float radians) {
        eulerTarget_.y = radians;
        eulerYEnabled_ = true;
        eulerYRelative_ = false;
        return *this;
    }
    TweenMod& rotateYBy(float radians) {
        eulerTarget_.y = radians;
        eulerYEnabled_ = true;
        eulerYRelative_ = true;
        return *this;
    }

    // Rotate Z axis to angle (same as rotateTo for 2D)
    TweenMod& rotateZTo(float radians) {
        return rotateTo(radians);
    }
    TweenMod& rotateZBy(float radians) {
        return rotateBy(radians);
    }

    // Set explicit start euler angles
    TweenMod& rotateXFrom(float radians) {
        eulerFrom_.x = radians;
        eulerXFromSet_ = true;
        return *this;
    }
    TweenMod& rotateYFrom(float radians) {
        eulerFrom_.y = radians;
        eulerYFromSet_ = true;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Rotation 3D (Quaternion)
    // -------------------------------------------------------------------------

    // Rotate to quaternion (slerp)
    TweenMod& rotateTo(const Quaternion& q) {
        quatTarget_ = q;
        quatEnabled_ = true;
        return *this;
    }

    TweenMod& rotateFrom(const Quaternion& q) {
        quatFrom_ = q;
        quatFromSet_ = true;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Common settings
    // -------------------------------------------------------------------------

    TweenMod& duration(float seconds) {
        duration_ = seconds;
        return *this;
    }

    TweenMod& ease(EaseType type, EaseMode mode = EaseMode::InOut) {
        easeType_ = type;
        easeMode_ = mode;
        return *this;
    }

    TweenMod& delay(float seconds) {
        delay_ = seconds;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Control
    // -------------------------------------------------------------------------

    TweenMod& start() {
        if (!getOwner()) return *this;
        initializeTweens();
        elapsed_ = -delay_;
        playing_ = true;
        completed_ = false;
        return *this;
    }

    TweenMod& pause() {
        playing_ = false;
        return *this;
    }

    TweenMod& resume() {
        if (!completed_) playing_ = true;
        return *this;
    }

    TweenMod& reset() {
        elapsed_ = 0.0f;
        playing_ = false;
        completed_ = false;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Getters
    // -------------------------------------------------------------------------

    bool isPlaying() const { return playing_; }
    bool isComplete() const { return completed_; }
    float getProgress() const {
        if (duration_ <= 0.0f) return 1.0f;
        return std::max(0.0f, std::min(elapsed_ / duration_, 1.0f));
    }

protected:
    // -------------------------------------------------------------------------
    // Mod lifecycle
    // -------------------------------------------------------------------------

    void earlyUpdate() override {
        if (!playing_ || completed_) return;

        Node* node = getOwner();
        if (!node) return;

        elapsed_ += static_cast<float>(getDeltaTime());

        // Still in delay period
        if (elapsed_ < 0) return;

        float t = getProgress();
        float easedT = trussc::ease(t, easeType_, easeMode_);

        // Apply position
        if (posEnabled_) {
            Vec3 current = posFrom_.lerp(posTo_, easedT);
            node->setPos(current);
        }

        // Apply scale
        if (scaleEnabled_) {
            Vec3 current = scaleFrom_.lerp(scaleTo_, easedT);
            node->setScale(current);
        }

        // Apply 2D rotation (Z-axis)
        if (rotEnabled_) {
            float current = rotFrom_ + (rotTo_ - rotFrom_) * easedT;
            node->setRot(current);
        }

        // Apply 3D euler rotations
        if (eulerXEnabled_ || eulerYEnabled_) {
            Vec3 euler = node->getEuler();
            if (eulerXEnabled_) {
                euler.x = eulerFrom_.x + (eulerTo_.x - eulerFrom_.x) * easedT;
            }
            if (eulerYEnabled_) {
                euler.y = eulerFrom_.y + (eulerTo_.y - eulerFrom_.y) * easedT;
            }
            node->setEuler(euler);
        }

        // Apply quaternion rotation
        if (quatEnabled_) {
            Quaternion current = Quaternion::slerp(quatFrom_, quatTo_, easedT);
            node->setQuaternion(current);
        }

        // Check completion
        if (t >= 1.0f) {
            playing_ = false;
            completed_ = true;
            complete.notify();
        }
    }

private:
    // -------------------------------------------------------------------------
    // Initialize from/to values when start() is called
    // -------------------------------------------------------------------------

    void initializeTweens() {
        Node* node = getOwner();
        if (!node) return;

        // Position
        if (posEnabled_) {
            if (!posFromSet_) {
                posFrom_ = node->getPos();
            }
            if (posRelative_) {
                posTo_ = posFrom_ + posTarget_;
            } else {
                posTo_ = posTarget_;
            }
        }

        // Scale
        if (scaleEnabled_) {
            if (!scaleFromSet_) {
                scaleFrom_ = node->getScale();
            }
            if (scaleRelative_) {
                scaleTo_ = Vec3(scaleFrom_.x * scaleTarget_.x,
                               scaleFrom_.y * scaleTarget_.y,
                               scaleFrom_.z * scaleTarget_.z);
            } else {
                scaleTo_ = scaleTarget_;
            }
        }

        // 2D Rotation
        if (rotEnabled_) {
            if (!rotFromSet_) {
                rotFrom_ = node->getRot();
            }
            if (rotRelative_) {
                rotTo_ = rotFrom_ + rotTarget_;
            } else {
                rotTo_ = rotTarget_;
            }
        }

        // 3D Euler X
        if (eulerXEnabled_) {
            if (!eulerXFromSet_) {
                eulerFrom_.x = node->getEuler().x;
            }
            if (eulerXRelative_) {
                eulerTo_.x = eulerFrom_.x + eulerTarget_.x;
            } else {
                eulerTo_.x = eulerTarget_.x;
            }
        }

        // 3D Euler Y
        if (eulerYEnabled_) {
            if (!eulerYFromSet_) {
                eulerFrom_.y = node->getEuler().y;
            }
            if (eulerYRelative_) {
                eulerTo_.y = eulerFrom_.y + eulerTarget_.y;
            } else {
                eulerTo_.y = eulerTarget_.y;
            }
        }

        // Quaternion
        if (quatEnabled_) {
            if (!quatFromSet_) {
                quatFrom_ = node->getQuaternion();
            }
            quatTo_ = quatTarget_;
        }
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    float duration_ = 1.0f;
    float delay_ = 0.0f;
    float elapsed_ = 0.0f;
    EaseType easeType_ = EaseType::Cubic;
    EaseMode easeMode_ = EaseMode::InOut;
    bool playing_ = false;
    bool completed_ = false;

    // Position
    bool posEnabled_ = false;
    bool posRelative_ = false;
    bool posFromSet_ = false;
    Vec3 posTarget_;
    Vec3 posFrom_;
    Vec3 posTo_;

    // Scale
    bool scaleEnabled_ = false;
    bool scaleRelative_ = false;
    bool scaleFromSet_ = false;
    Vec3 scaleTarget_;
    Vec3 scaleFrom_;
    Vec3 scaleTo_;

    // 2D rotation (Z-axis)
    bool rotEnabled_ = false;
    bool rotRelative_ = false;
    bool rotFromSet_ = false;
    float rotTarget_ = 0.0f;
    float rotFrom_ = 0.0f;
    float rotTo_ = 0.0f;

    // 3D euler rotation
    bool eulerXEnabled_ = false;
    bool eulerYEnabled_ = false;
    bool eulerXRelative_ = false;
    bool eulerYRelative_ = false;
    bool eulerXFromSet_ = false;
    bool eulerYFromSet_ = false;
    Vec3 eulerTarget_;
    Vec3 eulerFrom_;
    Vec3 eulerTo_;

    // Quaternion rotation
    bool quatEnabled_ = false;
    bool quatFromSet_ = false;
    Quaternion quatTarget_;
    Quaternion quatFrom_;
    Quaternion quatTo_;
};

} // namespace trussc
