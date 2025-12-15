#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    //settings.setPixelPerfect(true);
    settings.setSize(1280, 720);
    settings.setTitle("05_3d_primitives - 3D Primitives Demo");
    return tc::runApp<tcApp>(settings);
}
