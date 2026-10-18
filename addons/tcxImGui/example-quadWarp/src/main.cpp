#include "tcApp.h"

int main(int argc, char** argv) {
    WindowSettings settings;
    settings.setSize(1000, 700);
    settings.setTitle("ImGui through a quad warp");
    if (argc == 2 && string(argv[1]) == "--self-check") return runSelfCheck(settings);
    return TC_RUN_APP(tcApp, settings);
}
