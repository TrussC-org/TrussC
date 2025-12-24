#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.enableDebugInput = true;
    return runApp<tcApp>(settings);
}
