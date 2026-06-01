#pragma once

#include <TrussC.h>

namespace tcx {

class QuadWarp {
public:
    QuadWarp();
    
    void setup();
    void setSourceRect(const tc::Rect& rect);
    void setTargetRect(const tc::Rect& rect);
    
    void update();
    void draw();
    
    tc::Mat4 getMatrix() const;
    
    // Input control
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const { return inputEnabled_; }
    void toggleInput() { setInputEnabled(!inputEnabled_); }

    // Draw UI (outline and anchors)
    void drawUI();

    // UI Customization
    void setAnchorSize(float size) { anchorSize_ = size; }
    void setUIColor(const tc::Color& c) { uiColor_ = c; }
    void setUIHoverColor(const tc::Color& c) { uiHoverColor_ = c; }
    void setUISelectedColor(const tc::Color& c) { uiSelectedColor_ = c; }

    void save(const std::string& path = "quadwarp.json");
    void load(const std::string& path = "quadwarp.json");

    tc::Vec2 srcPoints[4];
    tc::Vec2 dstPoints[4];

private:
    void onMouseMoved(tc::MouseMoveEventArgs& e);
    void onMousePressed(tc::MouseEventArgs& e);
    void onMouseDragged(tc::MouseDragEventArgs& e);
    void onMouseReleased(tc::MouseEventArgs& e);
    void onKeyPressed(tc::KeyEventArgs& e);

    int selectedIndex_ = -1;
    int hoverIndex_ = -1;
    float anchorSize_ = 6.0f;
    float nudgeAmount_ = 0.2f;
    bool inputEnabled_ = false;
    
    tc::Color uiColor_ = tc::Color(0.0f, 0.8f, 0.0f);
    tc::Color uiHoverColor_ = tc::Color(1.0f, 1.0f, 0.0f);
    tc::Color uiSelectedColor_ = tc::Color(1.0f, 0.2f, 0.2f);

    // Event listeners (auto-disconnect on destruction or reset)
    tc::EventListener mouseMoveListener_;
    tc::EventListener mousePressListener_;
    tc::EventListener mouseDragListener_;
    tc::EventListener mouseReleaseListener_;
    tc::EventListener keyPressListener_;
};

} // namespace tcx
