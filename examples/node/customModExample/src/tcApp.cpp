// =============================================================================
// customModExample - Attach reusable behavior to nodes with custom Mods.
//
// Draggable consumes the press to capture dragging. Shake removes the previous
// offset before updating; FollowMouse uses window coordinates.
// =============================================================================

#include "tcApp.h"

// Mod source code:

bool Draggable::onMousePress(const MouseEventArgs&) {
    // true = this node takes the press, so the following drag events come here
    return true;
}

bool Draggable::onMouseDrag(const MouseDragEventArgs& e) {
    // getOwner() is the node this Mod is attached to; move it by the mouse movement
    getOwner()->setPos(getOwner()->getPos() + Vec3(e.globalDelta));
    return true;
}

void Shake::earlyUpdate() {
    // runs before update(): take back last frame's jitter first,
    // so the shake never adds up and the dragged position stays put
    getOwner()->setPos(getOwner()->getPos() - offset);
}

void Shake::update() {
    // pick a new small jitter and remember it for the next earlyUpdate()
    offset = Vec3(random(-3, 3), random(-3, 3), 0);
    getOwner()->setPos(getOwner()->getPos() + offset);
}

void FollowMouse::update() {
    // move 5% of the remaining distance each frame: fast when far, slow when close
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

// Main app code:

void tcApp::setup() {
    // Draggable circle
    auto circle = make_shared<Circle>();
    circle->setRect(100, 160, 70, 70);
    // needed for mouse hit-testing; the follower below gets no input, so it skips this
    circle->enableEvents();
    circle->addMod<Draggable>();
    addChild(circle);

    // Draggable box that also shakes
    auto box = make_shared<Box>();
    box->setRect(300, 160, 70, 70);
    box->enableEvents();
    box->addMod<Draggable>();
    box->addMod<Shake>();
    addChild(box);

    // Dot that follows the mouse
    auto follower = make_shared<DotNode>();
    follower->setPos(500, 300);
    follower->addMod<FollowMouse>();
    addChild(follower);
}

void tcApp::update() {
    // Empty on purpose: the Mods attached in setup() update their nodes
    // every frame, so the app doesn't need to move anything here.
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);
    setColor(1.0f);
    drawBitmapString("Drag the circle and box. The box also shakes.", 30, 35);
    drawBitmapString("The other circle follows the mouse.", 30, 55);
}
