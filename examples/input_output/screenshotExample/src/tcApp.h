#pragma once

#include "TrussC.h"
#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Screenshot save path
    std::filesystem::path savePath;
    int screenshotCount = 0;

    // Screen drawing content (for demo)
    float time = 0.0f;
};
