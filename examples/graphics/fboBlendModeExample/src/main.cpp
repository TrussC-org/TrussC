// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 540);
    settings.setTitle("fboBlendModeExample - TrussC");

    return TC_RUN_APP(tcApp, settings);
}
