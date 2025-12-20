// =============================================================================
// main.cpp - threadExample entry point
// =============================================================================
//
// Sample of multi-threaded programming using Thread.
// Implementation based on oF's threadExample.
//

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(640, 480);
    settings.setTitle("threadExample - TrussC");

    return runApp<tcApp>(settings);
}
