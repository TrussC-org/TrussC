// Complete native implementation: validation is not replaced by fixture assertions.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace trussc::internal { void setExitReasonIfEmpty(const char*) {} }
struct Settings { int width, height; bool pixelPerfect; bool highDpi = true; };
struct Platform { static constexpr bool isDesktop() { return true; } };
float getDisplayScaleFactor() { return 2; }
namespace internal {
sapp_desc buildWindowDescriptor(const Settings& settings) {
    LAUNCHER
    return desc;
}
}
sapp_desc normalDescriptor(const Settings& settings) { NORMAL_LAUNCHER }
sapp_desc baseDescriptor(const Settings& settings) {
    // Launcher sizing from next/window 4b26a8b1, before pixel creation overrides.
    sapp_desc desc = {};
    if (settings.pixelPerfect) {
        float displayScale = getDisplayScaleFactor();
        desc.width = static_cast<int>(settings.width / displayScale);
        desc.height = static_cast<int>(settings.height / displayScale);
    } else {
        desc.width = settings.width;
        desc.height = settings.height;
    }
    return desc;
}
bool tinyScale = false, tinySecondary = false, baseMode = false;
Settings requested{};
bool pixelPerfectMode() { return requested.pixelPerfect; }
sapp_window_desc secondaryDescriptor(const Settings& settings) {
    sapp_window_desc d = {};
    SECONDARY
    return d;
}
void initialized() {
    std::printf("native=%dx%d\n", sapp_width(), sapp_height());
    if (tinySecondary) _sapp_tc.dpi = 9.6e-7f;
    if (tinyScale || tinySecondary) {
        assert(_sapp_tc.dpi > 0 && _sapp_tc.dpi / 96 < 2e-8f);
        if (tinyScale) assert(sapp_width() == 640 && sapp_height() == 400 && sapp_dpi_scale() == 1);
        auto secondary = secondaryDescriptor({640,400,true});
        const auto window = sapp_create_window(&secondary);
        assert(window.id == 0);
        std::puts("tiny-scale secondary rejected; existing main remains valid");
    } else {
        auto descriptor = secondaryDescriptor(requested);
        if (baseMode) descriptor._tc_pixel_width = descriptor._tc_pixel_height = 0;
        const auto secondary = sapp_create_window(&descriptor);
        assert(secondary.id != 0);
        std::printf("secondary=%dx%d\n", sapp_window_framebuffer_width(secondary), sapp_window_framebuffer_height(secondary));
        sapp_destroy_window(secondary);
    }
    sapp_quit();
}
int main(int argc, char** argv) {
    assert(argc == 5);
    const Settings settings{std::atoi(argv[2]), std::atoi(argv[3]), bool(std::atoi(argv[4]))};
    requested = settings;
    baseMode = std::strcmp(argv[1], "base") == 0;
    sapp_desc d = baseMode ? baseDescriptor(settings) : normalDescriptor(settings);
    std::setbuf(stdout, nullptr);
    std::printf("descriptor=%dx%d\n", d.width, d.height);
    d.high_dpi = true;
    tinyScale = std::strcmp(argv[1], "tiny") == 0;
    tinySecondary = std::strcmp(argv[1], "tiny-secondary") == 0;
    if (tinyScale) d.high_dpi = false;
    d.init_cb = initialized;
    sapp_run(&d); // Both backends validate/default sizes in shared X11 creation.
}
