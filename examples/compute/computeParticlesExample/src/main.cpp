// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(640, 480);

    settings.setTitle("computeParticlesExample");

    return TC_RUN_APP(tcApp, settings);
}
