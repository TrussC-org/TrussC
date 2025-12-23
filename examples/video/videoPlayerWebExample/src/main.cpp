// =============================================================================
// videoPlayerWebExample - Video playback sample for Web
// =============================================================================
// Web-only VideoPlayer sample. Autoplays Big Buck Bunny on startup.

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("videoPlayerWebExample - TrussC");

    return runApp<tcApp>(settings);
}
