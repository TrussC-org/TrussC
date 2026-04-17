// =============================================================================
// videoPlayerExample - Video playback sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("videoPlayerExample");

    return TC_RUN_APP(tcApp, settings);
}
