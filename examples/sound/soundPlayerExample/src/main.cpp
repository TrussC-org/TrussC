// =============================================================================
// main.cpp - サウンド再生サンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("soundPlayerExample - TrussC");

    return runApp<tcApp>(settings);
}
