// =============================================================================
// main.cpp - Loop Architecture Demo
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("loopModeExample - Loop Architecture");

    return TC_RUN_APP(tcApp, settings);
}
