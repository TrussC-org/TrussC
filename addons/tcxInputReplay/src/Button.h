// One toolbar action, handled by the Node tree's picking and mouse dispatch.
// Replayed clicks leave the UI alone while still reaching the App.
#pragma once
#include <TrussC.h>
namespace tcx::inputreplay {
class Button : public tc::RectNode {
public:
    Button(std::string label, std::function<void()> onClick)
        : label_(std::move(label)), onClick_(std::move(onClick)) {
        enableEvents();
        setSize(70, 20);
    }
    void setLabel(const std::string& label) { label_ = label; }
    void setActiveColor(tc::Color color) { color_ = color; }
    void clearActiveColor() { color_ = tc::Color(0.28f, 0.30f, 0.34f); }
    void draw() override {
        auto color = color_;
        const float shade = pressed_ ? 0.85f : isMouseOver() ? 1.15f : 1.0f;
        color.r = (std::min)(1.0f, color.r * shade);
        color.g = (std::min)(1.0f, color.g * shade);
        color.b = (std::min)(1.0f, color.b * shade);
        tc::setColor(color);
        tc::fill();
        tc::drawRectRounded(0, 0, getWidth(), getHeight(), 4);
        tc::setColor(1.0f);
        tc::drawBitmapString(label_, getWidth() / 2, getHeight() / 2, tc::Direction::Center, tc::Direction::Center);
    }
protected:
    bool onMousePress(const tc::MouseEventArgs& e) override {
        if (!e.injected) { pressed_ = true; onClick_(); }
        return true;
    }
    bool onMouseRelease(const tc::MouseEventArgs& e) override {
        if (!e.injected) pressed_ = false;
        return true;
    }
private:
    std::string label_;
    std::function<void()> onClick_;
    tc::Color color_{0.28f, 0.30f, 0.34f};
    bool pressed_ = false;
};
} // namespace tcx::inputreplay
