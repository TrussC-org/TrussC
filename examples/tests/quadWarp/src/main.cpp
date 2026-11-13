#include "../../../../addons/tcxImGui/example-quadWarp/src/tcApp.h"

int runSelfCheck(const WindowSettings& settings);

int main(int argc, char** argv) {
    if (argc > 1 && (argc != 2 || string(argv[1]) != "--self-check")) return 2;
    WindowSettings settings;
    settings.setSize(1000, 700);
    return runSelfCheck(settings);
}
