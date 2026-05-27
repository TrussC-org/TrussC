#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 760);
    settings.setTitle("imageOpsExample - Image halve / resize / crop / mirror");
    return TC_RUN_APP(tcApp, settings);
}
