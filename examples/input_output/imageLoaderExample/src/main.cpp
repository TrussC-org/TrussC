// =============================================================================
// main.cpp - 画像読み込みサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("imageLoaderExample - Image Loading Demo");

    return runApp<tcApp>(settings);
}
