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
    vector<GrabberFrame> frames_;  // reused scratch for getBufferFrames()

    // last kStrip frames; slots are overwritten in turn (head_ wraps around)
    static constexpr int kStrip = 8;
    Texture tex_[kStrip];
    uint64_t tUs_[kStrip] = {};  // capture timestamp per slot
    int head_ = 0;               // next slot to overwrite
    size_t total_ = 0;
};
