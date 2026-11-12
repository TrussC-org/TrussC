#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

// Consume the press to capture dragging; these nodes use window coordinates.
struct Draggable : Mod {
    bool onMousePress(const MouseEventArgs&) override { return true; }
    bool onMouseDrag(const MouseDragEventArgs& e) override {
        getOwner()->setPos(getOwner()->getPos() + Vec3(e.globalDelta));
        return true;
    }
};

// Remove the previous jitter before adding a new offset around the base position.
struct Shake : Mod {
    Vec3 offset;
    void earlyUpdate() override { getOwner()->setPos(getOwner()->getPos() - offset); }
    void update() override {
        offset = Vec3(random(-3, 3), random(-3, 3), 0);
        getOwner()->setPos(getOwner()->getPos() + offset);
    }
};

// Move toward the mouse in window coordinates each frame.
struct FollowMouse : Mod {
    void update() override {
        getOwner()->setPos(getOwner()->getPos().lerp(Vec3(getMousePos()), 0.05f));
    }
};

struct Circle : RectNode {
    void draw() override { setColor(0.3f, 0.7f, 1.0f); drawCircle(35, 35, 35); }
};

struct Box : RectNode {
    void draw() override { setColor(1.0f, 0.6f, 0.3f); drawRect(0, 0, 70, 70); }
};

struct DotNode : Node {
    void draw() override { setColor(1.0f); drawCircle(0, 0, 15); }
};

class tcApp : public App {
public:
    void setup() override {
        auto circle = make_shared<Circle>();
        circle->setRect(100, 160, 70, 70);
        circle->enableEvents();
        circle->addMod<Draggable>();
        addChild(circle);

        auto box = make_shared<Box>();
        box->setRect(300, 160, 70, 70);
        box->enableEvents();
        box->addMod<Draggable>();
        box->addMod<Shake>();
        addChild(box);

        auto follower = make_shared<DotNode>();
        follower->setPos(500, 300);
        follower->addMod<FollowMouse>();
        addChild(follower);
    }

    void draw() override {
        clear(0.08f, 0.08f, 0.1f);
        setColor(1.0f);
        drawBitmapString("Drag the circle and box. The box also shakes.", 30, 35);
        drawBitmapString("The other circle follows the mouse.", 30, 55);
    }
};
