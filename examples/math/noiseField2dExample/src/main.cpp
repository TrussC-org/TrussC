#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setHighDpi(false);
    return TC_RUN_APP(tcApp, settings);
}
