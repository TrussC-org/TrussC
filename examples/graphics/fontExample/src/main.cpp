// =============================================================================
// main.cpp - TrueType フォントサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("fontExample - TrussC");

    return tc::runApp<tcApp>(settings);
}
