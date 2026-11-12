#include "../../../graphics/grabScreenExample/src/tcApp.h"

int runSelfCheck(const WindowSettings& settings);

int main(int argc, char** argv) {
    WindowSettings settings;
    settings.setSize(800, 500);
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "--self-check") continue;
        if (string(argv[i]) == "--portrait") settings.setSize(402, 874);
        else return 2;
    }
    return runSelfCheck(settings);
}
