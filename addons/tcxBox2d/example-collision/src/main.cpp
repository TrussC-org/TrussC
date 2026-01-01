// =============================================================================
// example-collision - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;
    settings.title = "example-collision";

    return runApp<tcApp>(settings);
}
