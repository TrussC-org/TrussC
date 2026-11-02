// #999: daily Linux / Xvfb regression. A static destructor must never issue GPU
// calls: the driver can already be unloaded at process exit. With sokol_gfx
// still valid, the exit-time cleanup of retired GPU nodes must leave them alone.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>

using namespace tc;

namespace {
int failures = 0;
bool completed = false;

void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class StaticGpuCleanupApp : public App {
public:
    void setup() override {
        setFps(VSYNC);
    }

    void update() override {
        Texture texture;
        texture.allocate(4, 4);
        const sg_image image = texture.getImage();
        check("texture has a valid image", sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        // Releasing the texture retires its handles; the next drain would destroy them.
        texture.clear();
        check("sokol_gfx is valid (the exit-time case)", sg_isvalid());
        check("retired image is still valid before exit cleanup", sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        // This is what the static cleanup object runs at process exit.
        internal::discardRetiredAtExit();
        check("exit cleanup leaves the retired image to the driver",
              sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        completed = true;
        exitApp();
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(32, 32);
        settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<StaticGpuCleanupApp>(settings, failures, [] {
            check("update ran", completed);
        });
    }
#else
    (void)argc;
    (void)argv;
#endif
    std::printf("SKIP: staticGpuCleanup needs --gpu-check on Linux with a display\n");
    return 0;
}
