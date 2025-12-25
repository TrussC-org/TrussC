#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.title = "timerExample";

    runApp<tcApp>(settings);
    return 0;
}
