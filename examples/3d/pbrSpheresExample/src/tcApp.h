#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// pbrSpheresExample - PBR material parameter grid
// 5x5 sphere grid: X axis = roughness, Y axis = metallic.

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
    Material materials[5][5];
};
