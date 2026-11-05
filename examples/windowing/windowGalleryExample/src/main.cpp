// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    // Closing the main window quits, gallery windows included.
    return TC_RUN_APP(tcApp, WindowSettings()
        .setSize(640, 480)
        .setTitle("windowGalleryExample - main")
        .setQuitOnClose(true));
}
