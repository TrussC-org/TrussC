#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include <vector>

using namespace trussc;

// mouseExample - Mouse Input Demo
// Visualization of mouse position, buttons, drag, and scroll

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float dx, float dy) override;

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
