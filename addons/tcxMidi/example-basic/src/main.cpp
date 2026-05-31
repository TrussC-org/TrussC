#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "tcxMidi - basic";
    settings.width = 520;
    settings.height = 460;
    settings.highDpi = false;
    return TC_RUN_APP(tcApp, settings);
}
