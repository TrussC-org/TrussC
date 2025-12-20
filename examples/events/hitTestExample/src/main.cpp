// =============================================================================
// hitTestExample - Ray-based Hit Test sample
// =============================================================================

#include "tcApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    WindowSettings settings;
    settings.title = "hitTestExample";
    settings.width = 1280;
    settings.height = 720;

    runApp<tcApp>(settings);

    return 0;
}
