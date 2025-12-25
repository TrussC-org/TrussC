// =============================================================================
// clippingExample - Scissor Clipping demo
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("clippingExample - TrussC");

    return runApp<tcApp>(settings);
}
