// =============================================================================
// main.cpp - エントリーポイント
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(960, 720);
    settings.setTitle("01_shapes - TrussC");

    return tc::runApp<tcApp>(settings);
}
