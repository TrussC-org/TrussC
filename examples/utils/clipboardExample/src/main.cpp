#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 400;
    settings.title = "clipboardExample";

    // Clipboard buffer is default 64KB
    // For larger data, use settings.setClipboardSize(1024 * 1024) to expand

    runApp<tcApp>(settings);
    return 0;
}
