#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

using namespace trussc;

// imguiExample - Dear ImGui demo

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void cleanup() override;

private:
    // Variables for demo
    float sliderValue = 0.5f;
    int counter = 0;
    float clearColor[3] = {0.1f, 0.1f, 0.1f};
    bool showDemoWindow = false;
    char textBuffer[256] = "Hello, TrussC!";
};
