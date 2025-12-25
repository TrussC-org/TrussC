#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("03_math - Vector & Matrix Demo");
    settings.setHighDpi(false);
    return runApp<tcApp>(settings);
}
