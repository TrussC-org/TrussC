// =============================================================================
// main.cpp - UDP Socket Example
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("udpExample - UDP Socket Demo");

    return TC_RUN_APP(tcApp, settings);
}
