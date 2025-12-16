// =============================================================================
// micInputExample - マイク入力 FFT スペクトラム可視化サンプル
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1024, 600);
    settings.setTitle("micInputExample - TrussC");

    return tc::runApp<tcApp>(settings);
}
