#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("strokeExample");
    return runApp<tcApp>(settings);
}
