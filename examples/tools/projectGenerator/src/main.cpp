#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "TrussC Project Generator";
    settings.width = 500;
    settings.height = 520;
    return runApp<tcApp>(settings);
}
