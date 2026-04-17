// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(480, 320);

    return TC_RUN_APP(tcApp, settings);
}
