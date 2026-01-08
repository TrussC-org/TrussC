// =============================================================================
// lutFilterExample - LUT (Look-Up Table) color grading demo
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 540);
    settings.setTitle("lutFilterExample - TrussC");
    settings.setHighDpi(false);

    return runApp<tcApp>(settings);
}
