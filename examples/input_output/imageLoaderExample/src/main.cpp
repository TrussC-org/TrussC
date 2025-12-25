// =============================================================================
// main.cpp - Image Loading Sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("imageLoaderExample - Image Loading Demo");

    return runApp<tcApp>(settings);
}
