// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(960, 600);

    // Host (hot reload) on desktop, runApp<tcApp> elsewhere.
    return TC_RUN_APP(tcApp, settings);
}
