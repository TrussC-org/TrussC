#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Sound music;
    Sound sfx;

    std::string musicPath;
    bool musicLoaded = false;
    bool sfxLoaded = false;
};
