// Headless DPI scale (#841): without a window the scale is 1.0 (macOS keeps
// its documented deviation, the main screen's scale), so two sizes of one
// font never share a cache key.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

using namespace std;
using namespace tc;

namespace {
#include "../../asyncLoads/src/fontFixture.h"

int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
} // namespace

TC_CORE_TEST_MAIN() {
    const float dpi = getDpiScale();
    std::printf("headless getDpiScale() = %f\n", dpi);
#if defined(__APPLE__) && TARGET_OS_OSX
    check("headless DPI scale is the main screen's scale (macOS)", dpi >= 1.0f);
#else
    check("headless DPI scale is 1.0 without a window", dpi == 1.0f);
#endif

    const fs::path dir = fs::temp_directory_path() / "tc-headlessDpi";
    fs::create_directories(dir);
    const fs::path path = dir / "font.ttf";
    {
        const auto data = makeFont();
        ofstream out(path, ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), (streamsize)data.size());
    }
    Font small, large;
    small.load(path, 20);
    large.load(path, 91);
    check("both sizes load", small.isLoaded() && large.isLoaded());
    check("two sizes of one font get distinct atlases",
          small.getAtlas(0) && large.getAtlas(0) && small.getAtlas(0) != large.getAtlas(0));
    check("the larger size has the larger line height", small.getLineHeight() < large.getLineHeight());
    fs::remove_all(dir);
    return failures ? 1 : 0;
}
