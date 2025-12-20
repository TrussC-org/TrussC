// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("00_base - TrussC");

    return tc::runApp<tcApp>(settings);
}
