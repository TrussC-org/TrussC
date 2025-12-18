// =============================================================================
// main.cpp - TCP ソケットサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("tcpExample - TCP Socket Demo");

    return runApp<tcApp>(settings);
}
