#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("04_color - Color Space Demo");
    return runApp<tcApp>(settings);
}
