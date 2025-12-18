// =============================================================================
// main.cpp - UDP ソケットサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("udpExample - UDP Socket Demo");

    return runApp<tcApp>(settings);
}
