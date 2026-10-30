// Explicit Window controls use the same binding for every desktop Window.
#if defined(__linux__) && !defined(__ANDROID__)
#include <TrussC.h>
#include <cstdio>
using namespace tc;
namespace {
int titles = 0, sizes = 0, fullscreenCalls = 0;
bool fullscreen = false;
double width = 0, height = 0;
std::string title;
bool read(sapp_window, void*, sapp_tc_window_metadata& metadata) {
    metadata.fb_width = 640; metadata.fb_height = 480; metadata.dpi_scale = 1;
    metadata.sample_count = 1; metadata.color_format = SAPP_PIXELFORMAT_RGBA8;
    return true;
}
void setTitle(Window&, const char* value) { ++titles; title = value; }
void setSize(Window&, double w, double h, bool) { ++sizes; width = w; height = h; }
void setFullscreen(Window&, bool value) { ++fullscreenCalls; fullscreen = value; }
uint32_t phase(const Window&) { return fullscreen ? 2 : 0; }
void toggle(Window&) { ++fullscreenCalls; fullscreen = !fullscreen; }
}
int main() {
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    const auto missingPosition = getWindowPosition();
    const bool missingQueries = missingPosition.x == 0 && missingPosition.y == 0 &&
        getAspectRatio() == 0 && worldToScreen({1, 2, 3}) == Vec3{} &&
        screenToWorld({1, 2}) == Vec3{} && getScale() == 0;
    auto handle = internal::prepareLaunchWindow(WindowSettings{});
    internal::WindowNativeOperations operations{};
    operations.readMetadata = read; operations.setTitle = setTitle;
    operations.setSize = setSize; operations.setFullscreen = setFullscreen;
    operations.fullscreenPhase = phase; operations.toggleFullscreen = toggle;
    internal::bindWindow(*handle, {101}, operations, nullptr);
    handle->setApp<App>(); internal::applyPendingAppForTests(*handle);
    setWindowTitle("Target"); setWindowSize(321, 123); handle->setSize(321, 123);
    setFullscreen(true);
    const bool entered = isFullscreen();
    const int before = sizes;
    setWindowSize(99, 77); setWindowSize(-1, 77);
    const bool ignored = sizes == before;
    toggleFullscreen();
    const bool left = !isFullscreen();
    internal::pixelPerfectMode() = true;
    Window other;
    auto* previous = internal::currentWindowCtx();
    bool oddPixels = true;
    for (float dpi : {1.25f, 1.5f, 1.75f, 2.0f}) {
        handle->context().dpiScale = dpi;
        internal::currentWindowCtx() = &other.context();
        handle->getApp()->setSize(1001, 667);
        oddPixels &= width == 1001.0 / dpi && height == 667.0 / dpi && internal::currentWindowCtx() == &other.context();
    }
    internal::currentWindowCtx() = previous;
    auto& a = handle->context();
    a.fbWidth = 640; a.fbHeight = 480; a.dpiScale = 1;
    a.swapchainTarget.width = 640; a.swapchainTarget.height = 480;
    a.currentTarget = &a.swapchainTarget;
    a.currentTarget->currentMatrix = Mat4::translate(3, 4, 5);
    const bool soleQueries = getMatrix().m[3] == 3 && getAspectRatio() == 640.f / 480;
    Window peer;
    internal::WindowRequestAccess::bindForTests(peer, 202);
    peer.setApp<App>(); internal::applyPendingAppForTests(peer);
    auto& b = peer.context();
    b.fbWidth = 300; b.fbHeight = 200; b.dpiScale = 1;
    b.currentTarget->width = 300; b.currentTarget->height = 200;
    b.currentTarget->camera.currentViewW = 300; b.currentTarget->camera.currentViewH = 200;
    b.currentTarget->currentMatrix = Mat4::translate(7, 8, 9);
    bool ownedQueries;
    internal::currentWindowCtx() = &a;
    {
        internal::AppOwnerScope owner(internal::appOwner(*peer.getApp()));
        ownedQueries = getFramebufferWidth() == 300 && getMatrix().m[3] == 7 &&
            worldToScreen({0, 0, 0}).x == 150 && worldToScreen({0, 0, 0}).y == 100;
    }
    const bool ambiguousQueries = getWidth() == 0 && getAspectRatio() == 0 &&
        worldToScreen({1, 2, 3}) == Vec3{} && screenToWorld({1, 2}) == Vec3{} && getScale() == 0;
    internal::RenderTarget offscreen;
    offscreen.isFbo = true; offscreen.width = 40; offscreen.height = 20;
    offscreen.currentMatrix = Mat4::translate(11, 12, 13);
    a.currentTarget = &offscreen;
    const bool explicitFboQueries = getWidth() == 40 && getHeight() == 20 &&
        getMatrix().m[3] == 11 && worldToScreen({0, 0, 0}).x == 20;
    bool constructorFboQuery = false;
    struct ConstructorQuery : virtual App {
        explicit ConstructorQuery(bool& result) {
            result = tc::getWidth() == 300 && tc::getFramebufferWidth() == 300 && tc::getMatrix().m[3] == 7;
        }
    };
    auto candidate = internal::makeConstructedAppAt<ConstructorQuery>(&b, constructorFboQuery);
    auto retiredOwner = internal::appOwner(*candidate);
    internal::retireAppOwner(retiredOwner);
    bool retiredQueries;
    {
        internal::AppOwnerScope lateDestructor(retiredOwner);
        retiredQueries = tc::getWidth() == 0 && tc::getScale() == 0 &&
            tc::worldToScreen({1, 2, 3}) == Vec3{};
    }
    std::printf("mainWindowRouting: query receivers missing=%d sole=%d owned=%d ambiguous=%d Fbo=%d constructor=%d retired=%d\n",
        missingQueries, soleQueries, ownedQueries, ambiguousQueries, explicitFboQueries, constructorFboQuery, retiredQueries);
    a.currentTarget = &a.swapchainTarget;
    internal::WindowRequestAccess::teardown(peer);
    internal::currentWindowCtx() = previous;
    const bool okay = missingQueries && soleQueries && ownedQueries && ambiguousQueries && explicitFboQueries && constructorFboQuery && retiredQueries && titles == 1 && title == mcp::internal::markedWindowTitle("Target") && sizes == 6 && entered && left && ignored && fullscreenCalls == 2 && oddPixels;
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    std::printf("mainWindowRouting: explicit controls and omitted query resolution: %s\n", okay ? "PASS" : "FAIL");
    return okay ? 0 : 1;
}
#elif defined(_WIN32)
#include "../nativeWindow/win32.cpp"
#else
#include <cstdio>
int main() { std::puts("SKIP: Linux entry-point recording fixture"); }
#endif
