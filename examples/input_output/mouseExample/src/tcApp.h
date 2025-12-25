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

    void mousePressed(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void mouseMoved(Vec2 pos) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseScrolled(Vec2 delta) override;

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
