// =============================================================================
// main.cpp - TLS (HTTPS) Client Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1000, 700);
    settings.setTitle("tlsExample - TLS/HTTPS Client Demo");

    return runApp<tcApp>(settings);
}
