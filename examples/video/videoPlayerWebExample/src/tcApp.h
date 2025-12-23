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

private:
    VideoPlayer video_;
    bool showInfo_ = true;
    bool loading_ = true;

    string formatTime(float seconds);
};
