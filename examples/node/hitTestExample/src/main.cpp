// =============================================================================
// hitTestExample - Ray-based Hit Test sample
// =============================================================================

#include "tcApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    WindowSettings settings;
    settings.title = "hitTestExample";
    settings.setSize(960, 600);

    runApp<tcApp>(settings);

    return 0;
}
