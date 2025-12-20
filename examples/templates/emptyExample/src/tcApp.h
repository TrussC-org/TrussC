#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    // Key events
    void keyPressed(int key) override;
    void keyReleased(int key) override;

    // Mouse events
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float deltaX, float deltaY) override;

    // Window events
    void windowResized(int width, int height) override;

    // Exit
    void exit() override;

    // File drop
    void filesDropped(const vector<string>& files) override;
};
