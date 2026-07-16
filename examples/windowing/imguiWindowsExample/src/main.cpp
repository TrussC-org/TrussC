// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("imguiWindowsExample - main");

    return TC_RUN_APP(tcApp, settings);
}
