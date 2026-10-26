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
#ifdef SOKOL_GLCORE
bool tinyScale = false;
bool pixelPerfectMode() { return true; }
sapp_window_desc secondaryDescriptor(const Settings& settings) {
    sapp_window_desc d = {};
    SECONDARY
    return d;
}
#endif
void initialized() {
    std::printf("native=%dx%d\n", sapp_width(), sapp_height());
#ifdef SOKOL_GLCORE
    if (tinyScale) {
        assert(_sapp_tc.dpi > 0 && _sapp_tc.dpi / 96 < 2e-8f);
        assert(sapp_width() == 640 && sapp_height() == 400 && sapp_dpi_scale() == 1);
        auto secondary = secondaryDescriptor({640,400,true});
        const auto window = sapp_create_window(&secondary);
        assert(window.id == 0);
        std::puts("tiny-scale secondary rejected; main high DPI disabled remains valid");
    }
#endif
    sapp_quit();
}
int main(int argc, char** argv) {
    assert(argc == 5);
    const Settings settings{std::atoi(argv[2]), std::atoi(argv[3]), bool(std::atoi(argv[4]))};
    sapp_desc d = std::strcmp(argv[1], "base") == 0 ? baseDescriptor(settings) : normalDescriptor(settings);
    std::setbuf(stdout, nullptr);
    std::printf("descriptor=%dx%d\n", d.width, d.height);
#ifdef SOKOL_GLES3
    // Run the real EGL validation first, exactly as the review probe did.
    _sapp_tc_init_state(&d);
    std::puts("native validation passed");
    _sapp_tc_discard_state();
#endif
    d.high_dpi = true;
#ifdef SOKOL_GLCORE
    tinyScale = std::strcmp(argv[1], "tiny") == 0;
    if (tinyScale) d.high_dpi = false;
#endif
    d.init_cb = initialized;
    sapp_run(&d); // GLX's equivalent validation/default selection lives in creation.
}
