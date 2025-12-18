// =============================================================================
// videoGrabberExample - Webカメラ入力サンプル
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("videoGrabberExample - TrussC");

    return runApp<tcApp>(settings);
}
