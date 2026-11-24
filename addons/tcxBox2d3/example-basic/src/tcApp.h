#pragma once
#include <TrussC.h>
#include <tcxBox2d.h>

using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void mousePressed(const MouseEventArgs& e) override;
    void mouseDragged(const MouseDragEventArgs& e) override;
    void mouseReleased(const MouseEventArgs& e) override;

private:
    box2d::World world;
    vector<box2d::RigidBody2D*> bodies;
    EventListener contact;
    int contacts = 0;
};
