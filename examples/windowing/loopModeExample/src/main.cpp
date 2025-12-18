// =============================================================================
// main.cpp - Loop Architecture デモ
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("loopModeExample - Loop Architecture");

    return runApp<tcApp>(settings);
}
