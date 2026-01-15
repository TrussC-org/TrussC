#pragma once

#include <TrussC.h>
#include <tcxTcv.h>

using namespace std;
using namespace tc;
using namespace tcx;

int getArgCount();
char** getArgValues();

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(Vec2 pos, int button) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void filesDropped(const vector<string>& files) override;

private:
    TcvPlayer player_;
    bool loaded_ = false;

    // FPS tracking
    float lastTime_ = 0;
    float fps_ = 0;
    int frameCount_ = 0;

    // Seekbar
    Rect seekbarRect_;
    bool isDraggingSeekbar_ = false;
    bool wasPlayingBeforeDrag_ = false;

    void loadVideo(const string& path);
    void updateSeekbarFromMouse(float x);
    bool isInsideSeekbar(float x, float y);
};
