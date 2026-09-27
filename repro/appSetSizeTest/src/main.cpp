#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(760, 420);
    settings.setTitle("appSetSizeTest - main");
    // TC_PIXEL_PERFECT=1: rerun every check in pixel-perfect mode, where
    // setSize() takes framebuffer pixels (only differs from logical on HiDPI).
    if (const char* pp = getenv("TC_PIXEL_PERFECT"); pp && pp[0] == '1') settings.setPixelPerfect(true);
    return TC_RUN_APP(tcApp, settings);
}
