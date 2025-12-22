#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 400;
    settings.title = "clipboardExample";

    // 小さいバッファでオーバーフローをテスト（100バイト）
    settings.setClipboardSize(100);

    runApp<tcApp>(settings);
    return 0;
}
