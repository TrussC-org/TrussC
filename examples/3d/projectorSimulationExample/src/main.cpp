#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("projectorSimulationExample - TrussC");

    return TC_RUN_APP(tcApp, settings);
}
