#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// hotReloadLifecycle guest App
//
// Deliberately minimal: the regressions this test guards live in what the App
// BASE class touches on construction/destruction (AudioEngine listeners, the
// window-context root slot, Event COW listener lists). The extra update
// listener widens the churn on the global events() singleton.
// =============================================================================
class tcApp : public App {
public:
    tcApp() {
        updateListener_ = events().update.listen([this]() { ticks_++; });
    }

private:
    EventListener updateListener_;
    int ticks_ = 0;
};
