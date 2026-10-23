// Main operations use these same inline dispatchers on Web/iOS/Android/Pi.
// A plain unit main: no tcCoreTest or TC_CORE_TEST_MAIN in standalone tests.
#if defined(__linux__) && !defined(__ANDROID__)
#include <TrussC.h>
#include <cstdio>
namespace {
int titleCalls = 0, sizeCalls = 0, fullscreenCalls = 0;
bool fullscreen = false;
float liveScale = 1.0f;
std::string titleSeen;
int widthSeen = 0, heightSeen = 0;
}
extern "C" float sapp_dpi_scale() { return liveScale; }
extern "C" bool sapp_isvalid() { return true; }
extern "C" void sapp_set_window_title(const char* title) { ++titleCalls; titleSeen = title; }
extern "C" bool sapp_is_fullscreen() { return fullscreen; }
extern "C" void sapp_toggle_fullscreen() { ++fullscreenCalls; fullscreen = !fullscreen; }
namespace trussc {
void setWindowSizeLogical(int width, int height) { ++sizeCalls; widthSeen = width; heightSeen = height; }
}
int main() {
    using namespace tc;
    getMainThreadId();
    internal::pixelPerfectMode() = false;
    internal::createMainWindow(WindowSettings{});
    auto* main = internal::mainWindow();
    internal::WindowRequestAccess::openMain(*main);
    const bool attached = internal::attachMainApp(std::make_shared<App>());
    setWindowTitle("sapp main"); setWindowSize(321, 123);
    main->setSize(321, 123);
    setFullscreen(true);
    const bool entered = isFullscreen();
    toggleFullscreen();
    const bool left = !isFullscreen();
    internal::pixelPerfectMode() = true;
    liveScale = 2.0f;
    internal::WindowContext alternate;
    alternate.isMain = false; alternate.dpiScale = 7.0f;
    internal::currentWindowCtx() = &alternate;
    main->getApp()->setSize(642, 246);
    internal::currentWindowCtx() = nullptr;
    const bool ownWindowSize = sizeCalls == 3 && widthSeen == 321 && heightSeen == 123;
    const bool ok = attached && internal::currentWindow() == main && titleCalls == 1 &&
        titleSeen == (mcp::internal::isDevBuild() ? "sapp main [MCP]" : "sapp main") && main->getTitle() == titleSeen && ownWindowSize &&
        widthSeen == 321 && heightSeen == 123 && fullscreenCalls == 2 && entered && left;
    internal::destroyMainWindow();
    std::printf("mainWindowRouting: sapp main title/size/fullscreen: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
#else
#include <cstdio>
int main() { std::puts("SKIP: Linux entry-point recording fixture"); }
#endif
