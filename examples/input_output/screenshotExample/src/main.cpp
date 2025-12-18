#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;
    settings.title = "screenshotExample";

    runApp<tcApp>(settings);
    return 0;
}
