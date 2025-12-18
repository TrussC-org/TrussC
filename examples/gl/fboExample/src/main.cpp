// =============================================================================
// main.cpp - エントリーポイント
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("textureExample - TrussC");

    return runApp<tcApp>(settings);
}
