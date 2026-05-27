// =============================================================================
// main.cpp - Font vector-path demo (scale + rotate)
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1200, 760);
    settings.setTitle("fontPathExample - TrussC");

    return TC_RUN_APP(tcApp, settings);
}
