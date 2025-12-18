// =============================================================================
// main.cpp - threadChannelExample エントリーポイント
// =============================================================================
//
// ThreadChannel を使ったスレッド間通信のサンプル。
// oF の threadChannelExample を参考に実装。
//

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(580, 380);
    settings.setTitle("threadChannelExample - TrussC");

    return runApp<tcApp>(settings);
}
