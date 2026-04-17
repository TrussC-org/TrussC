// =============================================================================
// videoGrabberExample - Webcam input sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("videoGrabberExample - TrussC");

    return TC_RUN_APP(tcApp, settings);
}
