#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("nodeExample - Node System Demo");
    return runApp<tcApp>(settings);
}
