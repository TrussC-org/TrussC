// =============================================================================
// soundPlayerFFTExample - FFT Spectrum Visualization Sample
// =============================================================================
//
// Audio Credit:
// -----------------------------------------------------------------------------
// Track: "113 2b loose-pants 4.2 mono"
// Author: astro_denticle
// Source: https://freesound.org/people/astro_denticle/
// License: CC0 1.0 Universal (Public Domain)
//
// This audio file is released under CC0, meaning it is free to use for any
// purpose without attribution. We gratefully acknowledge astro_denticle for
// making this sound available to the community.
// -----------------------------------------------------------------------------

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("soundPlayerFFTExample - TrussC");

    return runApp<tcApp>(settings);
}
