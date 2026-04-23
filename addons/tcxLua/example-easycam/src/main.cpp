// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(960, 600);

    return TC_RUN_APP(tcApp, settings);
}
