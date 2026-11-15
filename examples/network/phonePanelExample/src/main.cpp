#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setTitle("Phone control panel");
    settings.setFullscreen(true);
    return TC_RUN_APP(tcApp, settings);
}
