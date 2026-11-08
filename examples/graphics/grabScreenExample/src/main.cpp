#include "tcApp.h"

int main(int argc, char** argv) {
    WindowSettings settings;
    settings.setSize(800, 500);
    settings.setTitle("grabScreenExample - TrussC");
    if (argc >= 2 && string(argv[1]) == "--self-check") {
        if (argc == 3 && string(argv[2]) == "--portrait") settings.setSize(402, 874);
        return runSelfCheck(settings);
    }
    return TC_RUN_APP(tcApp, settings);
}
