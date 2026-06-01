#pragma once

#include <TrussC.h>

#include "sol/sol.hpp"

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

    void updateFbo();
    void reset();

    std::string script;
    sol::state lua;
    Fbo fbo;

    float x = 0;
    float y = 0;
};
