#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    static int run(int argc, char* argv[]);
    void draw() override;
    void keyPressed(int key) override;
};
