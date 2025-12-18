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
    std::vector<VideoDeviceInfo> devices_;
    int currentDevice_ = 0;
    bool permissionGranted_ = false;
    bool permissionRequested_ = false;
    int frameCount_ = 0;
    int newFrameCount_ = 0;
};
