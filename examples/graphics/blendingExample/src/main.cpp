// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("blendingExample - TrussC");

    return runApp<tcApp>(settings);
}
