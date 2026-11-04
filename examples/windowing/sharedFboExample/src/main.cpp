// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(PANEL_W, PANEL_H);
    settings.setTitle("sharedFboExample - left");
    // The three windows show one picture: closing any of them quits.
    settings.setQuitOnClose(true);

    return TC_RUN_APP(tcApp, settings);
}
