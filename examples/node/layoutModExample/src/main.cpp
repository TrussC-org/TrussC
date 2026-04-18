// =============================================================================
// layoutModExample - LayoutMod Demo
// =============================================================================

#include "tcApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    WindowSettings settings;
    settings.title = "layoutModExample";
    settings.setSize(960, 600);

    TC_RUN_APP(tcApp, settings);

    return 0;
}
