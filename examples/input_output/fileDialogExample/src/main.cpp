// =============================================================================
// main.cpp - File Dialog Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("fileDialogExample - File Dialog Demo");

    return runApp<tcApp>(settings);
}
