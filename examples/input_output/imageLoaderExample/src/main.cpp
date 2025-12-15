// =============================================================================
// main.cpp - 画像読み込みサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("imageLoaderExample - Image Loading Demo");

    return tc::runApp<tcApp>(settings);
}
