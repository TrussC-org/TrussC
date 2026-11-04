// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("dialogWindowExample - main");
    // Closing the main window quits the app, dialog included. Without this the
    // dialog would outlive the main App it reports back to.
    settings.setQuitOnClose(true);

    return TC_RUN_APP(tcApp, settings);
}
