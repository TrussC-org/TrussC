#include <TrussC.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

// Stable, distinct native dimensions keep headless read-through observable.
extern "C" float __wrap_sapp_dpi_scale() { return 2.f; }
extern "C" int __wrap_sapp_width() { return 1501; }
extern "C" int __wrap_sapp_height() { return 901; }

using namespace trussc;
namespace {
int failures = 0;
bool same(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
void readers(const char* phase) {
    auto& ctx = internal::currentWindowContext();
    // Prelaunch/headless have no resolved Window. Keep the closed-handle
    // checks on an explicit receiver, independently of the non-window context.
    Window closed;
    auto* window = &closed;
    const bool native = headless::isActive();
    const float dpi = native ? sapp_dpi_scale() : ctx.dpiScale;
    const int width = native ? sapp_width() : ctx.fbWidth;
    const int height = native ? sapp_height() : ctx.fbHeight;
    const float publicDpi = native ? dpi : 0.f;
    const auto owner = internal::currentAppOwner();
    for (bool pixels : {false, true}) for (bool fbo : {false, true}) {
        // Publish all fixture state before starting workers; join them before
        // changing modes, targets or lifecycle state. Native values stay fixed.
        internal::pixelPerfectMode() = pixels;
        auto& target = internal::currentTarget();
        target.isFbo = fbo; target.width = 1001; target.height = 667;
        const float scale = pixels ? 1.f : publicDpi;
        const float drawingScale = fbo || pixels ? 1.f : dpi;
        const int drawingWidth = native || fbo ? int(1001 / drawingScale) : 0;
        const int drawingHeight = native || fbo ? int(667 / drawingScale) : 0;
        std::atomic<int> ready{0};
        auto reader = [&] {
            // Real App callbacks select their owner; worker threads do not
            // inherit the caller's TLS scope automatically.
            internal::AppOwnerScope scope(owner);
            ready.fetch_add(1);
            while (ready.load() != 2) std::this_thread::yield();
            unsigned failed = 0;
            for (int i = 0; i < 2000; ++i) {
                failed += getDpiScale() != publicDpi;
                failed += getFramebufferWidth() != width;
                failed += getFramebufferHeight() != height;
                failed += internal::captureWindowSize(ctx) != std::pair<size_t,size_t>(std::max(0,width),std::max(0,height));
                failed += getWindowWidth() != internal::framebufferToLogical(width,scale);
                failed += getWindowHeight() != internal::framebufferToLogical(height,scale);
                failed += getWindowSize() != Vec2(internal::framebufferToLogical(width,scale),internal::framebufferToLogical(height,scale));
                failed += !same(getAspectRatio(),height > 0 ? float(width)/height : 0.f);
                failed += getWidth() != drawingWidth;
                failed += getHeight() != drawingHeight;
                failed += getSize() != Vec2(drawingWidth,drawingHeight);
                failed += internal::targetPixelScale() != drawingScale;
                failed += internal::targetLogicalSize() != Vec2(1001/drawingScale,667/drawingScale);
                failed += internal::framebufferToLogical(width,getDpiScale()) != int(width/(publicDpi > 0 ? publicDpi : 1.f));
                failed += internal::appSizeDpi(getDpiScale()) != scale;
                failed += internal::appPointerScale(getDpiScale()) != (scale > 0 ? 1.f/scale : 1.f);
                failed += window->getWidth() != 0;
                failed += window->getHeight() != 0;
                failed += window->isOccluded();
            }
            return failed;
        };
        unsigned aFailures = 0, bFailures = 0;
        std::thread a([&] { aFailures = reader(); }), b([&] { bFailures = reader(); });
        a.join(); b.join();
        failures += aFailures + bFailures;
    }
    std::printf("concurrent-readers: %s: 304000 checks finished\n", phase);
}
struct HeadlessReaders : App {
    void setup() override { readers("headless setup"); exitApp(); }
};
}
int main(int argc, char**) {
    getMainThreadId();
    setLogLevel(LogLevel::Silent); // Prelaunch has no available window API.
    (void)internal::currentWindowContext();
    (void)internal::pixelPerfectMode();
    if (argc > 1) runHeadlessApp<HeadlessReaders>();
    else readers("pre-window");
    std::printf("concurrent-readers: %d failed\n", failures);
    return failures ? 1 : 0;
}
