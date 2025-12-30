#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("Mesh Texture Mapping Example");
    return runApp<tcApp>(settings);
}

