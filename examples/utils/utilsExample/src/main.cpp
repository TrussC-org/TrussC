#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.title = "Utils Example";

    return runApp<tcApp>(settings);
}
