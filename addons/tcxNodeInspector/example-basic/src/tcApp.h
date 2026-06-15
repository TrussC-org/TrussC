#pragma once

#include <TrussC.h>
#include <tcxImGui.h>
#include <tcxNodeInspector.h>
#include "scene.h"
using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    RectNode::Ptr sceneRoot_;
};
