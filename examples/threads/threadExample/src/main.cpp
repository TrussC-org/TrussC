// =============================================================================
// main.cpp - threadExample エントリーポイント
// =============================================================================
//
// tc::Thread を使ったマルチスレッドプログラミングのサンプル。
// oF の threadExample を参考に実装。
//

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(640, 480);
    settings.setTitle("threadExample - TrussC");

    return tc::runApp<tcApp>(settings);
}
