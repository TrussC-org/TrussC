#pragma once
#include "../events/tcEventArgs.h"
#include "../utils/tcAnnotations.h"
#include "sokol/sokol_app_tc.h"

namespace trussc {
namespace internal { class WindowRequestAccess; }

// An input request, independent of callback consumption and node transforms.
// Positions are window-local logical points, even with pixelPerfect enabled.
// Keyboard modifiers use SAPP_MODIFIER_* bits and inherit held keys. Flags
// describe this event; held queries change through actual key transitions.
class TC_LUA_SKIP InputEvent {
public:
    static InputEvent mouseMove(Vec2 pos, uint32_t modifiers = 0) {
        return pointer(SAPP_EVENTTYPE_MOUSE_MOVE, pos, -1, modifiers);
    }
    static InputEvent mousePress(Vec2 pos, int button = 0, uint32_t modifiers = 0) {
        return pointer(SAPP_EVENTTYPE_MOUSE_DOWN, pos, button, modifiers);
    }
    static InputEvent mouseRelease(Vec2 pos, int button = 0, uint32_t modifiers = 0) {
        return pointer(SAPP_EVENTTYPE_MOUSE_UP, pos, button, modifiers);
    }
    // Wheel units are unchanged; the position is the window's current pointer.
    static InputEvent mouseScroll(Vec2 scroll, uint32_t modifiers = 0) {
        InputEvent result(SAPP_EVENTTYPE_MOUSE_SCROLL, modifiers);
        result.event_.scroll_x = scroll.x; result.event_.scroll_y = scroll.y;
        return result;
    }
    static InputEvent keyPress(int key, uint32_t modifiers = 0, bool isRepeat = false) {
        InputEvent result(SAPP_EVENTTYPE_KEY_DOWN, modifiers);
        // Bound (platform): stay inside the SDK enum range before casting;
        // the shared validator rejects undefined codes within that range.
        result.event_.key_code = key >= 0 && key < SAPP_MAX_KEYCODES ? static_cast<sapp_keycode>(key) : SAPP_KEYCODE_INVALID;
        result.event_.key_repeat = isRepeat;
        return result;
    }
    static InputEvent keyRelease(int key, uint32_t modifiers = 0) {
        InputEvent result(SAPP_EVENTTYPE_KEY_UP, modifiers);
        // Bound (platform): stay inside the SDK enum range before casting;
        // the shared validator rejects undefined codes within that range.
        result.event_.key_code = key >= 0 && key < SAPP_MAX_KEYCODES ? static_cast<sapp_keycode>(key) : SAPP_KEYCODE_INVALID;
        return result;
    }
    static InputEvent touchPress(const TouchEventArgs& touches, uint32_t modifiers = 0) {
        return touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, touches, modifiers);
    }
    static InputEvent touchMove(const TouchEventArgs& touches, uint32_t modifiers = 0) {
        return touch(SAPP_EVENTTYPE_TOUCHES_MOVED, touches, modifiers);
    }
    static InputEvent touchRelease(const TouchEventArgs& touches, uint32_t modifiers = 0) {
        return touch(touches.cancelled ? SAPP_EVENTTYPE_TOUCHES_CANCELLED : SAPP_EVENTTYPE_TOUCHES_ENDED,
                     touches, modifiers);
    }

private:
    explicit InputEvent(sapp_event_type type, uint32_t modifiers) {
        event_.type = type; event_.modifiers = modifiers;
        event_.mouse_button = SAPP_MOUSEBUTTON_INVALID;
    }
    static InputEvent pointer(sapp_event_type type, Vec2 pos, int button, uint32_t modifiers) {
        InputEvent result(type, modifiers);
        result.event_.mouse_x = pos.x; result.event_.mouse_y = pos.y;
        result.event_.mouse_button = button >= SAPP_MOUSEBUTTON_LEFT && button <= SAPP_MOUSEBUTTON_MIDDLE
            ? static_cast<sapp_mousebutton>(button) : SAPP_MOUSEBUTTON_INVALID;
        return result;
    }
    static InputEvent touch(sapp_event_type type, const TouchEventArgs& touches, uint32_t modifiers) {
        InputEvent result(type, modifiers);
        result.event_.num_touches = touches.numTouches;
        // Bound (platform): copy only the SDK-sized array; invalid counts are
        // retained for rejection at injection, before any delivery.
        for (int i = 0; i < touches.numTouches && i < TouchEventArgs::MAX_TOUCHES; ++i) {
            const auto& point = touches.touches[i];
            auto& target = result.event_.touches[i];
            target.identifier = static_cast<uintptr_t>(point.id);
            target.pos_x = point.x; target.pos_y = point.y; target.changed = point.changed;
        }
        return result;
    }
    sapp_event event_{};
    friend class internal::WindowRequestAccess;
};
} // namespace trussc
