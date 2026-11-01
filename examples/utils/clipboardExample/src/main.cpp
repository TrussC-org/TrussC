#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.title = "clipboardExample";

    // The internal OS clipboard buffer has a fixed 64 KB capacity.

    TC_RUN_APP(tcApp, settings);
    return 0;
}
