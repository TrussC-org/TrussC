#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "tcxMidi - controller";
    settings.width = 460;
    settings.height = 520;
    settings.highDpi = false;
    return TC_RUN_APP(tcApp, settings);
}
