#pragma once

// =============================================================================
// AllFeaturesExample — CI's compile-and-link canary.
//
// setup() calls af::coverAll(), which references every documented public core
// API (coverage_generated.cpp, from docs/reference/emit-coverage.js) and the
// bundled addons listed in addons.make (coverage_manual.cpp). Nothing of it
// runs: it only has to compile and link on each platform. What does run is a
// small scene with an ImGui panel.
// =============================================================================

#include <TrussC.h>
#include <tcxImGui.h>
#include "coverage.h"

using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    float speed_ = 0.5f;
};
