#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 500);
    settings.setTitle("grabScreenExample - TrussC");
    return TC_RUN_APP(tcApp, settings);
}
