// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("toolPaletteExample - canvas");
    // Closing the canvas quits, palette included. The palette itself can be
    // closed without quitting (P opens it again).
    settings.setQuitOnClose(true);

    return TC_RUN_APP(tcApp, settings);
}
