// =============================================================================
// main.cpp - Audio Synth Example
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("audioRecorderExample - TrussC");

    return TC_RUN_APP(tcApp, settings);
}
