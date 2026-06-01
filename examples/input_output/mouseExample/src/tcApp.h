#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// mouseExample - Mouse Input Demo
// Visualization of mouse position, buttons, drag, and scroll

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

    void mousePressed(const MouseEventArgs& e) override;
    void mouseReleased(const MouseEventArgs& e) override;
    void mouseMoved(const MouseMoveEventArgs& e) override;
    void mouseDragged(const MouseDragEventArgs& e) override;
    void mouseScrolled(const ScrollEventArgs& e) override;

private:
    // Drag trail
    struct DragPoint {
        float x, y;
        int button;
    };
    std::vector<DragPoint> dragTrail;

    // Click positions
    struct ClickPoint {
        float x, y;
        int button;
        float alpha;  // For fade out
    };
    std::vector<ClickPoint> clickPoints;

    // Accumulated scroll value
    float scrollX = 0;
    float scrollY = 0;

    // Current mouse state
    bool isDragging = false;
    int currentButton = -1;
};
