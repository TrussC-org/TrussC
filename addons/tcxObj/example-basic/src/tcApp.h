#pragma once

#include <TrussC.h>
#include "tcxObj.h"

using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void filesDropped(const vector<fs::path>& files) override;

private:
    EasyCam cam_;
    Light light_;
    Material material_;
    Environment env_;
    Mesh sphereMesh_;

    ObjLoader loader_;
    bool hasModel_ = false;
};
