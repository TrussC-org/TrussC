#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("03_math - Vector & Matrix Demo");
    return tc::runApp<tcApp>(settings);
}
