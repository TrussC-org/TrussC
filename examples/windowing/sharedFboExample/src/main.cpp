// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    // The three windows show one picture: closing any of them quits.
    return TC_RUN_APP(tcApp, WindowSettings()
        .setSize(PANEL_W, PANEL_H)
        .setTitle("sharedFboExample - left")
        .setQuitOnClose(true));
}
