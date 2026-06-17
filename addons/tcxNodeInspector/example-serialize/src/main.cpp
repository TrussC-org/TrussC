// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1100, 720);

    return TC_RUN_APP(tcApp, settings);
}
