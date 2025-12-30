#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("Mesh Texture Mapping Example");
    return runApp<tcApp>(settings);
}

