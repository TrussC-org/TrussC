#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 720);
    settings.setTitle("pbrSpheresExample - TrussC");

    return runApp<tcApp>(settings);
}
