#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

// =============================================================================
// tcApp - Application class
// Inherit from App and override required methods
// =============================================================================

class tcApp : public App {
public:
    // Lifecycle
    void setup() override;
    void update() override;
    void draw() override;

    // Input events (override only what you need)
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;

private:
    Path wave;  // For testing
};
