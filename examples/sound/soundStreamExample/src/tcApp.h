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
    // music = streaming voice (file decoded on demand by StreamWorker thread)
    Sound music;
    // sfx = eager voice (PCM in RAM) — coexists with the stream in the mixer
    Sound sfx;

    std::filesystem::path musicPath;
    bool musicLoaded = false;
    bool sfxLoaded = false;
};
