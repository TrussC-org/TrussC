#pragma once

#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    VideoGrabber grabber_;
    bool flipH_ = true;  // 左右反転（デフォルトon）
};
