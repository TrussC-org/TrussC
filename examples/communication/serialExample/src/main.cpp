// =============================================================================
// main.cpp - エントリーポイント
// TrussC シリアル通信サンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("serialExample - TrussC");

    return runApp<tcApp>(settings);
}
