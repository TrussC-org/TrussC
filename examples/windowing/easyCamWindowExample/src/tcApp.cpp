#include "tcApp.h"

void tcApp::setup() {
    mainCam_.enableMouseInput();   // main window's camera

    WindowSettings ws;
    ws.setSize(520, 420);
    ws.setTitle("easyCamWindowExample - second");
    second_ = createWindow(ws);
    if (second_) {
        orbitApp_ = make_shared<OrbitApp>();
        second_->setApp(orbitApp_);
    }
}

void tcApp::draw() {
    clear(0.12f, 0.10f, 0.08f);
    mainCam_.begin();
    setColor(0.9f, 0.4f, 0.4f);
    noFill();
        drawBox(160);
        fill();
    setColor(0.4f, 1.0f, 0.5f);
    drawSphere(Vec3(-110, 0, 0), 22);
    mainCam_.end();
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("main window: drag to orbit (independent camera)", 16, 24);
}

void tcApp::exit() {
    if (second_ && second_->isOpen()) second_->close();
}
