#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.title = "consoleExample";

    TC_RUN_APP(tcApp, settings);
    return 0;
}
