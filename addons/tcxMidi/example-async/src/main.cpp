#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "tcxMidi - async";
    settings.width = 520;
    settings.height = 460;
    settings.highDpi = false;
    return TC_RUN_APP(tcApp, settings);
}
