#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("02_nodes - Node System Demo");
    return tc::runApp<tcApp>(settings);
}
