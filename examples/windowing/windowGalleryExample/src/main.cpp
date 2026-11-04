// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(640, 480);
    settings.setTitle("windowGalleryExample - main");
    // pixelPerfect is process-wide in v0.8.0: the launch window's value
    // applies to every window, so the gallery cannot vary it per window.
    settings.setPixelPerfect(false);
    // Closing the main window quits, gallery windows included.
    settings.setQuitOnClose(true);

    return TC_RUN_APP(tcApp, settings);
}
