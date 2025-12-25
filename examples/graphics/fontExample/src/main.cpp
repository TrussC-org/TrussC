// =============================================================================
// main.cpp - TrueType font sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("fontExample - TrussC");

    return runApp<tcApp>(settings);
}
