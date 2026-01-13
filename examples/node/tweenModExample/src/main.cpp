// =============================================================================
// tweenModExample - TweenMod Animation Demo
// =============================================================================

#include "tcApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    WindowSettings settings;
    settings.title = "tweenModExample";
    settings.setSize(960, 600);

    runApp<tcApp>(settings);

    return 0;
}
