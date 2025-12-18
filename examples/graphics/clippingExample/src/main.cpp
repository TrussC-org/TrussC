// =============================================================================
// clippingExample - Scissor Clipping のデモ
// =============================================================================

#include "tcApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    WindowSettings settings;
    settings.title = "clippingExample";
    settings.width = 1280;
    settings.height = 720;

    runApp<tcApp>(settings);

    return 0;
}
