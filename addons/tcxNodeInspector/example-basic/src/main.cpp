#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1000, 640);
    settings.setTitle("tcxNodeInspector Example");

    return TC_RUN_APP(tcApp, settings);
}
