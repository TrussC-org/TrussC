// =============================================================================
// customModExample - Attach reusable behavior to nodes with custom Mods.
//
// Draggable consumes the press to capture dragging. Shake removes the previous
// offset before updating; FollowMouse uses window coordinates.
// =============================================================================

#include "tcApp.h"

bool Draggable::onMousePress(const MouseEventArgs&) {
    return true;
}

bool Draggable::onMouseDrag(const MouseDragEventArgs& e) {
    getOwner()->setPos(getOwner()->getPos() + Vec3(e.globalDelta));
    return true;
}

void Shake::earlyUpdate() {
    getOwner()->setPos(getOwner()->getPos() - offset);
}

void Shake::update() {
    offset = Vec3(random(-3, 3), random(-3, 3), 0);
    getOwner()->setPos(getOwner()->getPos() + offset);
}

void FollowMouse::update() {
    getOwner()->setPos(getOwner()->getPos().lerp(Vec3(getMousePos()), 0.05f));
}

void Circle::draw() {
    setColor(0.3f, 0.7f, 1.0f);
    drawCircle(35, 35, 35);
}

void Box::draw() {
    setColor(1.0f, 0.6f, 0.3f);
    drawRect(0, 0, 70, 70);
}

void DotNode::draw() {
    setColor(1.0f);
    drawCircle(0, 0, 15);
}

void tcApp::setup() {
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

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);
    setColor(1.0f);
    drawBitmapString("Drag the circle and box. The box also shakes.", 30, 35);
    drawBitmapString("The other circle follows the mouse.", 30, 55);
}
