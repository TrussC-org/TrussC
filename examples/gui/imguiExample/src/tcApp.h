#pragma once

#include "tcBaseApp.h"
using namespace tc;

using namespace trussc;

// imguiExample - Dear ImGui のデモ

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void cleanup() override;

private:
    // デモ用の変数
    float sliderValue = 0.5f;
    int counter = 0;
    float clearColor[3] = {0.1f, 0.1f, 0.1f};
    bool showDemoWindow = false;
    char textBuffer[256] = "Hello, TrussC!";
};
