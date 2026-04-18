#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("Mesh Texture Mapping Example");
    return TC_RUN_APP(tcApp, settings);
}

