// =============================================================================
// main.cpp - File Dialog Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("fileDialogExample - File Dialog Demo");

    return TC_RUN_APP(tcApp, settings);
}
