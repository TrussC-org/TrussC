#pragma once
#include "tcEventArgs.h"

namespace trussc {

struct InputModifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

// A copied input payload in destination App coordinates. Window::injectInput
// supplies provenance and delivers fresh arguments through the Runtime gate.
class InputEvent {
public:
    static InputEvent keyPressed(int key, InputModifiers mods = {}, bool repeat = false) {
        InputEvent e(Kind::KeyPressed);
        e.key_ = key;
        e.repeat_ = repeat;
        e.mods_ = mods;
        return e;
    }
    static InputEvent keyPressed(const KeyEventArgs& event) {
        return keyPressed(event.key, modifiers(event), event.isRepeat);
    }
    static InputEvent keyReleased(int key, InputModifiers mods = {}) {
        InputEvent e(Kind::KeyReleased);
        e.key_ = key;
        e.mods_ = mods;
        return e;
    }
    static InputEvent keyReleased(const KeyEventArgs& event) {
        return keyReleased(event.key, modifiers(event));
    }
    static InputEvent mousePressed(Vec2 pos, MouseButton button, InputModifiers mods = {}) {
        InputEvent e(Kind::MousePressed);
        e.pos_ = pos;
        e.button_ = button;
        e.mods_ = mods;
        return e;
    }
    static InputEvent mousePressed(const MouseEventArgs& event) {
        return mousePressed(event.pos, receivedButton(event.button), modifiers(event));
    }
    static InputEvent mouseReleased(Vec2 pos, MouseButton button, InputModifiers mods = {}) {
        InputEvent e(Kind::MouseReleased);
        e.pos_ = pos;
        e.button_ = button;
        e.mods_ = mods;
        return e;
    }
    static InputEvent mouseReleased(const MouseEventArgs& event) {
        return mouseReleased(event.pos, receivedButton(event.button), modifiers(event));
    }
    static InputEvent mouseMoved(Vec2 pos, InputModifiers mods = {}) {
        InputEvent e(Kind::MouseMoved);
        e.pos_ = pos;
        e.mods_ = mods;
        return e;
    }
    template<class = void>
    static InputEvent mouseMoved(const MouseMoveEventArgs& event) {
        return mouseMoved(event.pos, modifiers(event));
    }
    static InputEvent mouseScrolled(Vec2 scroll, InputModifiers mods = {}) {
        InputEvent e(Kind::MouseScrolled);
        e.scroll_ = scroll;
        e.mods_ = mods;
        return e;
    }
    template<class = void>
    static InputEvent mouseScrolled(const ScrollEventArgs& event) {
        return mouseScrolled(event.scroll, modifiers(event));
    }
    static InputEvent touchPressed(const TouchEventArgs& event) {
        return InputEvent(Kind::TouchPressed, event);
    }
    static InputEvent touchMoved(const TouchEventArgs& event) {
        return InputEvent(Kind::TouchMoved, event);
    }
    static InputEvent touchReleased(const TouchEventArgs& event) {
        return InputEvent(Kind::TouchReleased, event);
    }
private:
    friend class Window;
    enum class Kind { KeyPressed, KeyReleased, MousePressed, MouseReleased,
                      MouseMoved, MouseScrolled, TouchPressed, TouchMoved, TouchReleased };
    explicit InputEvent(Kind kind) : kind_(kind) {}
    InputEvent(Kind kind, const TouchEventArgs& event) : kind_(kind) {
        touch_.numTouches = event.numTouches;
        touch_.cancelled = event.cancelled;
        // Bound (platform): copy at most MAX_TOUCHES array entries; the gate rejects the unchanged invalid count.
        for (int i = 0; i < event.numTouches && i < TouchEventArgs::MAX_TOUCHES; ++i)
            touch_.touches[i] = event.touches[i];
    }
    static MouseButton receivedButton(int button) {
        return button >= 0 && button <= 2 ? static_cast<MouseButton>(button) : MouseButton::None;
    }
    template<class T> static InputModifiers modifiers(const T& event) {
        return {event.shift, event.ctrl, event.alt, event.super};
    }
    Kind kind_;
    InputModifiers mods_;
    int key_ = 0;
    bool repeat_ = false;
    Vec2 pos_;
    Vec2 scroll_;
    MouseButton button_ = MouseButton::None;
    TouchEventArgs touch_;
};

} // namespace trussc
