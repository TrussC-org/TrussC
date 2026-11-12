#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

// -----------------------------------------------------------------------------
// Mods: reusable behavior. addMod<T>() attaches one to any node.
// -----------------------------------------------------------------------------

// Drag the node with the mouse. These nodes use window coordinates.
struct Draggable : Mod {
    bool onMousePress(const MouseEventArgs&) override {
        // true = this node takes the press, so the following drag events come here
        return true;
    }

    bool onMouseDrag(const MouseDragEventArgs& e) override {
        // getOwner() is the node this Mod is attached to; move it by the mouse movement
        getOwner()->setPos(getOwner()->getPos() + Vec3(e.globalDelta));
        return true;
    }
};

// Jitter the node around its position every frame.
struct Shake : Mod {
    Vec3 offset;

    void earlyUpdate() override {
        // runs before update(): take back last frame's jitter first,
        // so the shake never adds up and the dragged position stays put
        getOwner()->setPos(getOwner()->getPos() - offset);
    }

    void update() override {
        // pick a new small jitter and remember it for the next earlyUpdate()
        offset = Vec3(random(-3, 3), random(-3, 3), 0);
        getOwner()->setPos(getOwner()->getPos() + offset);
    }
};

// Move toward the mouse in window coordinates each frame.
struct FollowMouse : Mod {
    void update() override {
        // move 5% of the remaining distance each frame: fast when far, slow when close
        getOwner()->setPos(getOwner()->getPos().lerp(Vec3(getMousePos()), 0.05f));
    }
};

// -----------------------------------------------------------------------------
// Nodes: they only draw themselves. All movement comes from the Mods above.
// -----------------------------------------------------------------------------

struct Circle : RectNode {
    void draw() override {
        setColor(0.3f, 0.7f, 1.0f);
        drawCircle(35, 35, 35);
    }
};

struct Box : RectNode {
    void draw() override {
        setColor(1.0f, 0.6f, 0.3f);
        drawRect(0, 0, 70, 70);
    }
};

struct DotNode : Node {
    void draw() override {
        setColor(1.0f);
        drawCircle(0, 0, 15);
    }
};

// -----------------------------------------------------------------------------

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
};
