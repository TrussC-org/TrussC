#include "../../../graphics/grabScreenExample/src/tcApp.h"

int runSelfCheck(const WindowSettings& settings);

int main(int argc, char** argv) {
    WindowSettings settings;
    settings.setSize(800, 500);
    if (argc == 2 && string(argv[1]) == "--portrait") settings.setSize(402, 874);
    return runSelfCheck(settings);
}
