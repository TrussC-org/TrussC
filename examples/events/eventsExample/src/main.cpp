#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    return runApp<tcApp>(settings);
}
