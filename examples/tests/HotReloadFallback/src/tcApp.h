#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// HotReloadFallback - build canary for an app that uses TC_HOT_RELOAD (#329)
//
// tcApp.cpp has TC_HOT_RELOAD(tcApp). On desktop the build splits into a
// host executable + guest shared library. On web / Android / iOS there is no
// hot reload: the configure step and the pre-build check must both ignore the
// macro (tc_hot_reload_decide() in core/cmake/tc_hot_reload_scan.cmake) and
// build a normal app. The daily desktop and web sweeps build this project, so
// a mismatch between the two fails there. No addons, on purpose.
class tcApp : public App {
public:
    void setup() override;
    void draw() override;
};
