// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(480, 100);

    settings.setTitle("computeReadbackAsyncExample");

    return TC_RUN_APP(tcApp, settings);
}
