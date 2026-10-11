#pragma once

// =============================================================================
// tcTouchMouse - touch-as-mouse mapping (internal)
//
// Decides which mouse event, if any, a touch event produces when touch-as-mouse
// is on. The first finger down is the primary touch and is the only one that
// drives the mouse:
//   - BEGAN:  a primary that is not among the touches is stale (its ENDED /
//             CANCELLED never arrived) and is dropped. Then, with no primary,
//             the first changed touch becomes the primary and gives one press
//             at its position.
//   - MOVED:  if the primary is among the touches, a drag at its position.
//   - ENDED / CANCELLED: if the primary is among the changed touches, one
//             release at its position, and the primary is cleared.
//   - An event with no touches clears the primary.
// Other fingers only produce touch events. Touches are identified by sokol's
// uintptr_t identifier (on iOS it is the UITouch pointer), and the primary is
// looked up by that identifier, not by its index in the touch array.
//
// Kept free of sokol and window state so it can be driven headless
// (core/tests/touchAsMouse). The instance used by the event callback is
// internal::touchMouseMapper() (tcGlobal.cpp).
// =============================================================================

#include <cstdint>
#include <optional>

namespace trussc {
namespace internal {

enum class TouchPhase { Began, Moved, Ended, Cancelled };

// One touch point as delivered by the platform (position already in app units).
struct TouchSample {
    uintptr_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool changed = false;
};

// The mouse event a touch event maps to.
struct TouchMouseAction {
    enum class Kind { None, Press, Drag, Release };
    Kind kind = Kind::None;
    float x = 0.0f;
    float y = 0.0f;
};

class TouchMouseMapper {
public:
    TouchMouseAction update(TouchPhase phase, const TouchSample* touches, int numTouches) {
        TouchMouseAction action;
        if (numTouches <= 0 || touches == nullptr) {
            primaryId_.reset();
            return action;
        }

        switch (phase) {
            case TouchPhase::Began:
                // A primary that is no longer down lost its end event; without
                // this it would block every later press.
                if (primaryId_ && !findPrimary(touches, numTouches)) {
                    primaryId_.reset();
                }
                if (!primaryId_) {
                    for (int i = 0; i < numTouches; i++) {
                        if (touches[i].changed) {
                            primaryId_ = touches[i].id;
                            action.kind = TouchMouseAction::Kind::Press;
                            action.x = touches[i].x;
                            action.y = touches[i].y;
                            break;
                        }
                    }
                }
                break;
            case TouchPhase::Moved:
                if (const TouchSample* p = findPrimary(touches, numTouches)) {
                    action.kind = TouchMouseAction::Kind::Drag;
                    action.x = p->x;
                    action.y = p->y;
                }
                break;
            case TouchPhase::Ended:
            case TouchPhase::Cancelled:
                if (const TouchSample* p = findPrimary(touches, numTouches); p && p->changed) {
                    action.kind = TouchMouseAction::Kind::Release;
                    action.x = p->x;
                    action.y = p->y;
                    primaryId_.reset();
                }
                break;
        }
        return action;
    }

    bool hasPrimary() const { return primaryId_.has_value(); }
    void reset() { primaryId_.reset(); }

private:
    const TouchSample* findPrimary(const TouchSample* touches, int numTouches) const {
        if (!primaryId_) return nullptr;
        for (int i = 0; i < numTouches; i++) {
            if (touches[i].id == *primaryId_) return &touches[i];
        }
        return nullptr;
    }

    std::optional<uintptr_t> primaryId_;
};

// The mapper used by the main window's event callback (defined in tcGlobal.cpp).
TouchMouseMapper& touchMouseMapper();

} // namespace internal
} // namespace trussc
