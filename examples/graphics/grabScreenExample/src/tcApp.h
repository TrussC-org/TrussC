#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    bool grabRequested_ = false;
    Pixels pixels_;
    Texture thumbnail_;
    EventListener afterFrameListener_;

    friend class SelfCheckApp;
};

// Kept separately so the reference example stays focused on capturing a frame.
int runSelfCheck(const WindowSettings& settings);
