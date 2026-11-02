#include <TrussC.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace trussc;
namespace {
int failures = 0;
bool same(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
void readers(const char* phase) {
    auto& ctx = internal::currentWindowContext();
    auto* window = internal::resolveWindow();
    const float dpi = sapp_dpi_scale();
    const int width = sapp_width(), height = sapp_height();
    for (bool pixels : {false, true}) for (bool fbo : {false, true}) {
        // Publish all fixture state before starting workers; join them before
        // changing modes, targets or lifecycle state. Native values stay fixed.
        internal::pixelPerfectMode() = pixels;
        auto& target = internal::currentTarget();
        target.isFbo = fbo; target.width = 1001; target.height = 667;
        const float scale = pixels ? 1.f : dpi, drawingScale = fbo ? 1.f : scale;
        std::atomic<int> ready{0};
        auto reader = [&] {
            ready.fetch_add(1);
            while (ready.load() != 2) std::this_thread::yield();
            unsigned failed = 0;
            for (int i = 0; i < 2000; ++i) {
                failed += getDpiScale() != dpi;
                failed += getFramebufferWidth() != width;
                failed += getFramebufferHeight() != height;
                failed += internal::captureWindowSize(ctx) != std::pair<size_t,size_t>(std::max(0,width),std::max(0,height));
                failed += getWindowWidth() != int(width/scale);
                failed += getWindowHeight() != int(height/scale);
                failed += getWindowSize() != Vec2(int(width/scale),int(height/scale));
                failed += !same(getAspectRatio(),float(width)/height);
                failed += getWidth() != int(1001/drawingScale);
                failed += getHeight() != int(667/drawingScale);
                failed += getSize() != Vec2(int(1001/drawingScale),int(667/drawingScale));
                failed += internal::targetPixelScale() != drawingScale;
                failed += internal::targetLogicalSize() != Vec2(1001/drawingScale,667/drawingScale);
                failed += internal::framebufferToLogical(width,getDpiScale()) != int(width/dpi);
                failed += internal::appSizeDpi(getDpiScale()) != scale;
                failed += internal::appPointerScale(getDpiScale()) != 1.f/scale;
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
    void setup() override { readers("headless setup"); requestExit(); }
};
}
int main(int argc, char**) {
    getMainThreadId();
    (void)internal::currentWindowContext();
    (void)internal::pixelPerfectMode();
    if (argc > 1) runHeadlessApp<HeadlessReaders>();
    else readers("pre-window");
    std::printf("concurrent-readers: %d failed\n", failures);
    return failures ? 1 : 0;
}
