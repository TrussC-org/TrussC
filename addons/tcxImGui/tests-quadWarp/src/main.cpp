#include "../../example-quadWarp/src/tcApp.h"

int runSelfCheck(const WindowSettings& settings);

int main() {
    WindowSettings settings;
    settings.setSize(1000, 700);
    return runSelfCheck(settings);
}
