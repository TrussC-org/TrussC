// =============================================================================
// main.cpp - Async Dialog Example
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(480, 800);
    settings.setTitle("asyncDialogExample");

    return runApp<tcApp>(settings);
}
