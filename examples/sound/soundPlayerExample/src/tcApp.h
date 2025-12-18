#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "tc/sound/tcSound.h"

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
