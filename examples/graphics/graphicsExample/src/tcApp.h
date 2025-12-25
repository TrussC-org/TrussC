#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

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
    void mousePressed(Vec2 pos, int button) override;
    void mouseDragged(Vec2 pos, int button) override;

private:
    Path wave;  // For testing
};
