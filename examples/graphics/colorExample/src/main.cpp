#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("04_color - Color Space Demo");
    return TC_RUN_APP(tcApp, settings);
}
