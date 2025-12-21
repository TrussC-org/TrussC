// =============================================================================
// videoPlayerExample - Video playback sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("videoPlayerExample - TrussC");

    return runApp<tcApp>(settings);
}
