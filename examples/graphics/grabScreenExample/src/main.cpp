#include "tcApp.h"

int main(int argc, char** argv) {
    WindowSettings settings;
    settings.setSize(800, 500);
    settings.setTitle("grabScreenExample - TrussC");
    if (argc == 2 && string(argv[1]) == "--self-check") return runSelfCheck(settings);
    return TC_RUN_APP(tcApp, settings);
}
