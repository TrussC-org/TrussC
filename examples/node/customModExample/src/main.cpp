// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(640, 400);

    settings.setTitle("customModExample");

    return TC_RUN_APP(tcApp, settings);
}
