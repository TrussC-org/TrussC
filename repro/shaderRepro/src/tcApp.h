#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

#include "shaders/test.glsl.h"

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

private:
    Shader solidShader;   // test 1 (uniform last-write-wins) + test 4 (fbo leak)
    Shader texShader;     // test 2 (texture last-write-wins)
    Shader fboShader;     // test 4 (separate instance so tests stay independent)
    Image imgBlue;
    Image imgYellow;
    Fbo fbo;
    uint64_t frameCount = 0;
};
