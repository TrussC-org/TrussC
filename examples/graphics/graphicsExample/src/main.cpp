// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("01_shapes - TrussC");

    return runApp<tcApp>(settings);
}
