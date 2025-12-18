// =============================================================================
// main.cpp - TrueType フォントサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("fontExample - TrussC");

    return runApp<tcApp>(settings);
}
