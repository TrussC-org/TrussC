#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.title = "clipboardExample";

    // Clipboard buffer is default 64KB
    // For larger data, use settings.setClipboardSize(1024 * 1024) to expand

    TC_RUN_APP(tcApp, settings);
    return 0;
}
