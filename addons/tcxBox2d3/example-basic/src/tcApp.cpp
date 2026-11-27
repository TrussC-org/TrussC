#include "tcApp.h"

void tcApp::setup() {
    world.setup();
    auto floor = make_shared<Node>();
    floor->setPos(480, 560);
    addChild(floor);
    auto* ground = floor->addMod<box2d::RigidBody2D>(world, b2_staticBody);
    ground->addBox(960, 40);
    contact = ground->onContactBegin.listen([this](box2d::ContactEvent&) {
        ++contacts;
    });

    for (int i = 0; i < 5; ++i) {
        auto node = make_shared<Node>();
        node->setPos(320 + i * 70, 80 + i * 40);
        addChild(node);
        auto* body = node->addMod<box2d::RigidBody2D>(world, b2_dynamicBody);
        body->addCircle(25);
        bodies.push_back(body);
    }
}

void tcApp::update() {
    world.update();
}

void tcApp::draw() {
    clear(0.12f);
    setColor(0.8f);
    drawRect(0, 540, 960, 40);
    for (auto* body : bodies) {
        drawCircle(body->getPosition(), 25);
    }
    drawBitmapString("Drag a circle. Ground contacts: " + to_string(contacts), 20, 30);
}

void tcApp::mousePressed(const MouseEventArgs& e) {
    for (auto* body : bodies) {
        if (body->getPosition().distance(e.pos) <= 25) {
            world.startDrag(body, e.pos);
            break;
        }
    }
}

void tcApp::mouseDragged(const MouseDragEventArgs& e) {
    world.updateDrag(e.pos);
}

void tcApp::mouseReleased(const MouseEventArgs&) {
    world.endDrag();
}
