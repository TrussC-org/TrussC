// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1040, 860);

    return TC_RUN_APP(tcApp, settings);
}
