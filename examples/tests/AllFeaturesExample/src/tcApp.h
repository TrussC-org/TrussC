#pragma once

#include <TrussC.h>

// Core Features are included in TrussC.h

// Addons
#include "tcxBox2d.h"
#include "tcxOsc.h"
#include "tcTlsClient.h"
#include "tcWebSocketClient.h"
#include "tcLut.h"

using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void keyReleased(int key) override;

    void mousePressed(const MouseEventArgs& e) override;
    void mouseReleased(const MouseEventArgs& e) override;
    void mouseMoved(const MouseMoveEventArgs& e) override;
    void mouseDragged(const MouseDragEventArgs& e) override;
    void mouseScrolled(const ScrollEventArgs& e) override;

    void windowResized(int width, int height) override;
    void filesDropped(const vector<string>& files) override;
    void exit() override;

    // Addon instances to verify linking
    tcx::box2d::World box2d;
    tc::OscSender oscSender;
    tc::OscReceiver oscReceiver;

    // LUT addon
    tcx::lut::Lut3D lut;
};