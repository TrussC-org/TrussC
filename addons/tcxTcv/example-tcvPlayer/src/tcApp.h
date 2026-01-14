#pragma once

#include <TrussC.h>
#include <tcxTcv.h>

using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void filesDropped(const vector<string>& files) override;

private:
    TcvPlayer player_;
    bool loaded_ = false;

    // FPS tracking
    float lastTime_ = 0;
    float fps_ = 0;
    int frameCount_ = 0;

    void loadVideo(const string& path);
};
