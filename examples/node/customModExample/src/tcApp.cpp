// =============================================================================
// customModExample - Attach reusable behavior to nodes with custom Mods.
//
// Draggable consumes the press to capture dragging. Shake removes the previous
// offset before updating; FollowMouse uses window coordinates.
// =============================================================================

#include "tcApp.h"

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
