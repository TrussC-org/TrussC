#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1280, 720);
    settings.setTitle("tcxDepthRecord - record + playback");

    return TC_RUN_APP(tcApp, settings);
}
