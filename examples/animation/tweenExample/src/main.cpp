// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(960, 800);
    settings.setTitle("Tween Example");

    return TC_RUN_APP(tcApp, settings);
}
