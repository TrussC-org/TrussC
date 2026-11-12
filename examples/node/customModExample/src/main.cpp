#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "customModExample";
    settings.setSize(640, 400);
    TC_RUN_APP(tcApp, settings);
    return 0;
}
