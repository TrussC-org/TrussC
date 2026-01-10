// =============================================================================
// example-lut - LUT (Look-Up Table) color grading demo
// =============================================================================

#pragma once

#include <TrussC.h>
#include "tcLut.h"
using namespace std;
using namespace tc;
using namespace tcx::lut;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Camera
    VideoGrabber grabber;

    // LUTs (generated in code, no .cube files needed)
    static constexpr int NUM_LUTS = 8;
    tcx::lut::Lut3D luts[NUM_LUTS];
    string lutNames[NUM_LUTS] = {
        "Identity", "Vintage", "Cinematic", "Film Noir",
        "Warm", "Cool", "Cyberpunk", "Custom (.cube)"
    };

    // LUT Shader (uses new Shader system)
    tcx::lut::LutShader lutShader;

    // Selected LUT for fullscreen view (-1 = grid view)
    int selectedLut = -1;

    void drawWithLut(float x, float y, float w, float h, int lutIndex);
    void drawOriginal(float x, float y, float w, float h);
};
