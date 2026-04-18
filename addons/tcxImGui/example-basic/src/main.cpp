#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 800);
    return TC_RUN_APP(tcApp, settings);
}
