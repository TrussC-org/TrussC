// =============================================================================
// micInputExample - Microphone Input FFT Spectrum Visualization Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1024, 600);
    settings.setTitle("micInputExample - TrussC");

    return runApp<tcApp>(settings);
}
