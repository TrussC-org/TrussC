// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1000, 610);
    settings.setTitle("Custom Ease Example");

    return TC_RUN_APP(tcApp, settings);
}
