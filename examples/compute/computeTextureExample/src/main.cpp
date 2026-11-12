// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(256, 256);

    settings.setTitle("computeTextureExample");

    return TC_RUN_APP(tcApp, settings);
}
