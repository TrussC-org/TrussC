#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(1000, 700);
    settings.setTitle("ImGui through a quad warp");
    return TC_RUN_APP(tcApp, settings);
}
