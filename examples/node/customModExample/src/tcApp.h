#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

// Consume the press to capture dragging; these nodes use window coordinates.
struct Draggable : Mod {
    bool onMousePress(const MouseEventArgs&) override;
    bool onMouseDrag(const MouseDragEventArgs& e) override;
};

// Remove the previous jitter before adding a new offset around the base position.
struct Shake : Mod {
    Vec3 offset;
    void earlyUpdate() override;
    void update() override;
};

// Move toward the mouse in window coordinates each frame.
struct FollowMouse : Mod {
    void update() override;
};

struct Circle : RectNode {
    void draw() override;
};

struct Box : RectNode {
    void draw() override;
};

struct DotNode : Node {
    void draw() override;
};

class tcApp : public App {
public:
    void setup() override;

    void draw() override;
};
