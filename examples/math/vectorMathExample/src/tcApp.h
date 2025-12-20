#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include <vector>
#include <cstdio>   // snprintf

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;

private:
    // Particle
    struct Particle {
        Vec2 pos;
        Vec2 vel;
        Vec2 acc;
        float radius;
        float hue;
        float life;
        float maxLife;
    };

    std::vector<Particle> particles_;

    // Demo mode
    int mode_ = 0;
    static constexpr int NUM_MODES = 4;

    // Helper
    void drawVec2Demo();
    void drawRotationDemo();
    void drawLerpDemo();
    void drawParticleDemo();

    void spawnParticle(float x, float y);
    void updateParticles();
    void drawParticles();

    // Color conversion (hue: 0-1)
    void setColorHSB(float h, float s, float b, float a = 1.0f);
};
