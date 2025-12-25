#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// strokeMeshExample - StrokeMesh (thick line drawing) demo
// Cap Types: BUTT, ROUND, SQUARE
// Join Types: MITER, ROUND, BEVEL
// Variable width stroke support

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    std::vector<StrokeMesh> strokes;        // Open shapes (3x3 grid)
    std::vector<StrokeMesh> closedStrokes;  // Closed shapes (star)
    StrokeMesh variableStroke;              // Variable width stroke

    float strokeWidth = 20.0f;
};
