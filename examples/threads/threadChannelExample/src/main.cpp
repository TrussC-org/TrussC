// =============================================================================
// main.cpp - threadChannelExample entry point
// =============================================================================
//
// Sample of inter-thread communication using ThreadChannel.
// Implementation based on oF's threadChannelExample.
//

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(580, 380);
    settings.setTitle("threadChannelExample - TrussC");

    return runApp<tcApp>(settings);
}
