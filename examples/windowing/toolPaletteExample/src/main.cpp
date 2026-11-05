// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    // Closing the canvas quits, palette included. Closing the palette does not.
    return TC_RUN_APP(tcApp, WindowSettings()
        .setSize(800, 600)
        .setTitle("toolPaletteExample - canvas")
        .setQuitOnClose(true));
}
