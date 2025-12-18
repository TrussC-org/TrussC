#pragma once

#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;

private:
    int mode = 0;  // 0: VSync, 1: 固定60fps, 2: 固定30fps, 3: イベント駆動
    int updateCount = 0;
    int drawCount = 0;
    float lastResetTime = 0;
};
