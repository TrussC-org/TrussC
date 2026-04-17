#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 720);
    settings.setTitle("normalMapExample - TrussC");

    return runApp<tcApp>(settings);
}
