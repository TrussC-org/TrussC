// =============================================================================
// main.cpp - Async Dialog Example
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(480, 800);
    settings.setTitle("asyncDialogExample");

    return TC_RUN_APP(tcApp, settings);
}
