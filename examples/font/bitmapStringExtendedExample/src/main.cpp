#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(1280, 800);
    settings.setTitle("bitmapStringExtendedExample");
    return TC_RUN_APP(tcApp, settings);
}
