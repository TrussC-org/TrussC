#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("screenshotExample - TrussC");

    return runApp<tcApp>(settings);
}
