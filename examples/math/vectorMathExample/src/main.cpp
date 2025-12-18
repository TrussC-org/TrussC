#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("03_math - Vector & Matrix Demo");
    return runApp<tcApp>(settings);
}
