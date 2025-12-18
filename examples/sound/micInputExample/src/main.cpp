// =============================================================================
// micInputExample - マイク入力 FFT スペクトラム可視化サンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 600);
    settings.setTitle("micInputExample - TrussC");

    return runApp<tcApp>(settings);
}
