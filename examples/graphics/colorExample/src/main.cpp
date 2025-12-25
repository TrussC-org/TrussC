#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("04_color - Color Space Demo");
    return runApp<tcApp>(settings);
}
