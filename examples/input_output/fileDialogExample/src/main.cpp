// =============================================================================
// main.cpp - ファイルダイアログサンプル
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("fileDialogExample - File Dialog Demo");

    return tc::runApp<tcApp>(settings);
}
