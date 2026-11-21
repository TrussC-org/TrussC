#pragma once
#include <TrussC.h>
#include <tcxInputReplay.h>
using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;
};
