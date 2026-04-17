#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// normalMapExample - Normal map demo
// Shows the effect of normal maps on PBR surfaces.
// Left: flat (no normal map), Right: with procedural normal map.

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    EasyCam cam;
    Mesh sphereMesh;
    Light keyLight;
    Light fillLight;
    Environment env;
    Texture normalMapTex;
    Material matFlat;
    Material matBumped;
};
