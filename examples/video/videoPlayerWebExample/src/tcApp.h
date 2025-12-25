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
    VideoPlayer video_;
    bool showInfo_ = true;
    bool loading_ = true;

    string formatTime(float seconds);
};
