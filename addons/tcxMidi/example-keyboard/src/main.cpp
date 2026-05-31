#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.title = "tcxMidi - keyboard";
    settings.width = 860;
    settings.height = 240;
    settings.highDpi = false;
    return TC_RUN_APP(tcApp, settings);
}
