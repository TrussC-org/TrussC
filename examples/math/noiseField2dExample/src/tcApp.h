#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// noiseField2dExample - Perlin noise demo
// Flow field and noise texture visualization

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Display mode
    int mode = 0;
    static const int NUM_MODES = 4;

    // For animation
    float time = 0.0f;
    float noiseScale = 0.01f;
    float timeSpeed = 0.5f;

    // Particles (for flow field)
    struct Particle {
        float x, y;
        float prevX, prevY;
    };
    std::vector<Particle> particles;

    // Noise texture (drawn to image for efficiency)
    Image noiseImage;
    int lastWidth = 0;
    int lastHeight = 0;

    void drawNoiseTexture();
    void drawFlowField();
    void drawFlowParticles();
    void drawFbmTexture();
    void updateNoiseImage(bool useFbm);

    void resetParticles();
};
