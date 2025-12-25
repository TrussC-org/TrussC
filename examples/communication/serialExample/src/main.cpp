// =============================================================================
// main.cpp - Entry Point
// TrussC Serial Communication Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("serialExample - TrussC");

    return runApp<tcApp>(settings);
}
