// =============================================================================
// videoPlayerExample - Video playback sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("videoPlayerExample - TrussC");
    settings.enableDebugInput = true;  // Enable tcdebug input simulation

    return runApp<tcApp>(settings);
}
