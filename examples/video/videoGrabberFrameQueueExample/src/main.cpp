// =============================================================================
// videoGrabberFrameQueueExample - Timestamped frame queue sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 680);
    settings.setTitle("videoGrabberFrameQueueExample - TrussC");

    return TC_RUN_APP(tcApp, settings);
}
