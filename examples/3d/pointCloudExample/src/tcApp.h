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
    void rebuild(int n);

    EasyCam view;
    Mesh    cloud;                 // static point cloud (rebuilt only on N change)

    int        n_ = 250000;
    PointStyle style_ = PointStyle::Square;
    float      pointSize_ = 8.0f;  // logical px
};
