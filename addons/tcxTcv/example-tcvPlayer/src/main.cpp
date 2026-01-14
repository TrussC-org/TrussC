// =============================================================================
// main.cpp - TCV Player entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("TCV Player");

    return tc::runApp<tcApp>(settings);
}
