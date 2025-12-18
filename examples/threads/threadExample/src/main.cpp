// =============================================================================
// main.cpp - threadExample エントリーポイント
// =============================================================================
//
// Thread を使ったマルチスレッドプログラミングのサンプル。
// oF の threadExample を参考に実装。
//

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(640, 480);
    settings.setTitle("threadExample - TrussC");

    return runApp<tcApp>(settings);
}
