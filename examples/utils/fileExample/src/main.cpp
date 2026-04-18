#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(800, 600);
    settings.title = "File Example";

    return TC_RUN_APP(tcApp, settings);
}
