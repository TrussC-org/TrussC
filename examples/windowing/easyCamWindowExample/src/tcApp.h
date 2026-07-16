#pragma once

#include <TrussC.h>

using namespace std;
using namespace tc;

// An interactive EasyCam in a SECONDARY window. Each window gets its own
// camera with its own mouse interaction: EasyCam::enableMouseInput() binds to
// the window whose App calls it (see the multi-window contract in
// tcEasyCam.h), so orbiting one window never moves the other's camera.

class OrbitApp : public App {
public:
    void setup() override {
        cam_.enableMouseInput();   // binds to THIS window's mouse
    }
    void update() override {}
    void draw() override {
        clear(0.08f, 0.10f, 0.14f);
        cam_.begin();
        setColor(0.4f, 0.8f, 1.0f);
        noFill();
        drawBox(140);
        fill();
        setColor(1.0f, 0.6f, 0.2f);
        drawSphere(Vec3(90, 0, 0), 18);
        cam_.end();
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString("second window: drag to orbit (independent camera)", 16, 24);
    }
private:
    EasyCam cam_;
};

class tcApp : public App {
public:
    void setup() override;
    void update() override {}
    void draw() override;
    void exit() override;

private:
    shared_ptr<Window> second_;
    shared_ptr<OrbitApp> orbitApp_;
    EasyCam mainCam_;
};
