#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 400;
    settings.title = "clipboardExample";

    // クリップボードバッファはデフォルト64KB
    // 大きいデータを扱う場合は settings.setClipboardSize(1024 * 1024) などで拡張可能

    runApp<tcApp>(settings);
    return 0;
}
