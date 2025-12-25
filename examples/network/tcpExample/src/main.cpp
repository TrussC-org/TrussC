// =============================================================================
// main.cpp - TCP Socket Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("tcpExample - TCP Socket Demo");

    return runApp<tcApp>(settings);
}
