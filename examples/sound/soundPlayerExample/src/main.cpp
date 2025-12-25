// =============================================================================
// main.cpp - Sound Player Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("soundPlayerExample - TrussC");

    return runApp<tcApp>(settings);
}
