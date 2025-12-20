#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;

private:
    int mode = 0;  // 0: VSync, 1: Fixed 60fps, 2: Fixed 30fps, 3: Event-driven
    int updateCount = 0;
    int drawCount = 0;
    float lastResetTime = 0;
};
