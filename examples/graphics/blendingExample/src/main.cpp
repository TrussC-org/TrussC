// =============================================================================
// main.cpp - エントリーポイント
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("blendingExample - TrussC");

    return tc::runApp<tcApp>(settings);
}
