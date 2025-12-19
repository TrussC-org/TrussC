#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "OSC Example (Event)";
    settings.width = 700;
    settings.height = 500;
    settings.highDpi = false;
    return runApp<tcApp>(settings);
}
