#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("05_3d_primitives - 3D Primitives Demo");
    return runApp<tcApp>(settings);
}
