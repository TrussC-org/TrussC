#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setHighDpi(false);
    return runApp<tcApp>(settings);
}
