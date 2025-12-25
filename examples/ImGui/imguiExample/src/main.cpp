#include "TrussC.h"
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 800);
    return runApp<tcApp>(settings);
}
