#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    VideoGrabber grabber_;
    EventListener lostListener_;
    float lastTry_ = 0.0f;  // when setup() was last tried again
    bool flipH_ = true;  // Horizontal flip (default on)
};
