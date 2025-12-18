#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("05_3d_primitives - 3D Primitives Demo");
    return runApp<tcApp>(settings);
}
