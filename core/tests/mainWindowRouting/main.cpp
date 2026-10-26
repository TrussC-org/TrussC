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
double widthSeen = 0, heightSeen = 0;
int fractionalCalls = 0;
}
extern "C" float sapp_dpi_scale() { return liveScale; }
extern "C" bool sapp_isvalid() { return true; }
extern "C" void sapp_set_window_title(const char* title) { ++titleCalls; titleSeen = title; }
extern "C" bool sapp_is_fullscreen() { return fullscreen; }
extern "C" void sapp_toggle_fullscreen() { ++fullscreenCalls; fullscreen = !fullscreen; }
namespace trussc::internal {
void setMainWindowSizeLogical(int width, int height) { ++sizeCalls; widthSeen = width; heightSeen = height; }
void setMainWindowSizeFractionalX11(double width, double height) {
    ++sizeCalls; ++fractionalCalls; widthSeen = width; heightSeen = height;
}
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
    int ignoredNotices = 0;
    bool exactNotices = true;
    auto notices = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message.starts_with("[Window] ")) {
            ++ignoredNotices;
            exactNotices &= e.level == LogLevel::Notice && e.message ==
                "[Window] setWindowSize() ignored while fullscreen; call it once isFullscreen() returns false";
        }
    });
    const int sizesBefore = sizeCalls;
    setWindowSize(99, 77);
    setWindowSize(-1, 77);
    const bool ignored = sizeCalls == sizesBefore && ignoredNotices == 2 && exactNotices;
    toggleFullscreen();
    const bool left = !isFullscreen();
    internal::pixelPerfectMode() = true;
    liveScale = 2.0f;
    internal::WindowContext alternate;
    alternate.isMain = false; alternate.dpiScale = 7.0f;
    internal::currentWindowCtx() = &alternate;
    main->getApp()->setSize(642, 246);
    internal::currentWindowCtx() = nullptr;
    const bool ownWindowSize = sizeCalls == 3 && fractionalCalls == 1 && widthSeen == 321 && heightSeen == 123;
    const bool ok = attached && internal::currentWindow() == main && titleCalls == 1 &&
        titleSeen == (mcp::internal::isDevBuild() ? "sapp main [MCP]" : "sapp main") && main->getTitle() == titleSeen && ownWindowSize &&
        widthSeen == 321 && heightSeen == 123 && fullscreenCalls == 2 && entered && left && ignored;
    bool oddPixels = true;
    for (float dpi : {1.25f, 1.5f, 1.75f, 2.0f}) {
        liveScale = dpi;
        internal::currentWindowCtx() = &alternate;
        const int before = fractionalCalls;
        main->getApp()->setSize(1001, 667);
        oddPixels &= fractionalCalls == before + 1 && widthSeen == 1001.0 / dpi &&
            heightSeen == 667.0 / dpi && internal::currentWindowCtx() == &alternate;
    }
    internal::currentWindowCtx() = nullptr;
    internal::destroyMainWindow();
    std::printf("mainWindowRouting: sapp main title/size/fullscreen: %s\n", ok && oddPixels ? "PASS" : "FAIL");
    return ok && oddPixels ? 0 : 1;
}
#else
#include <cstdio>
int main() { std::puts("SKIP: Linux entry-point recording fixture"); }
#endif
