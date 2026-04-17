#include "tcApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    WindowSettings settings;
    settings.title = "grabExample";
    settings.setSize(650, 480);

    TC_RUN_APP(tcApp, settings);

    return 0;
}
