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
    void filesDropped(const vector<string>& files) override;

private:
    VideoPlayer video_;
    bool showInfo_ = true;
    string videoPath_;

    void loadVideo(const string& path);
    string formatTime(float seconds);
};
