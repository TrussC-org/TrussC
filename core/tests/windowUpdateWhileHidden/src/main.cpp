#include <TrussC.h>
#include "../../common/tcWindowTest.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("windowUpdateWhileHidden: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void hidden(Window& window, bool value) {
    auto metadata = internal::WindowRequestAccess::binding(window).metadata;
    metadata.trust_hidden_signals = true;
    metadata.occluded = value;
    internal::publishWindowMetadata(window, metadata);
}
void tick(Window& window, int seconds, bool gated) {
    internal::currentWindowCtx() = &internal::WindowRequestAccess::context(window);
    const internal::WindowTickPresentation presentation{[] {}, [] { return true; }, [] {}};
    internal::runWindowTick(std::chrono::steady_clock::time_point{} + std::chrono::seconds(seconds),
        .25, false, &presentation, gated);
}
}
TC_CORE_TEST_MAIN() {
    if (!Platform::isDesktop()) {
        std::puts("SKIP: windowUpdateWhileHidden desktop scheduler contract");
        return 0;
    }
    getMainThreadId();
    tcCoreTest::WindowFixture first(901), second(902);
    auto& window = *first.window;
    auto& ctx = internal::WindowRequestAccess::context(window);
    auto& other = internal::WindowRequestAccess::context(*second.window);
    internal::retainRuntimeWindow(first.window);
    internal::retainRuntimeWindow(second.window);
    ctx.fbWidth = ctx.fbHeight = 100;
    check("desktop defaults true", ctx.updateWhileHidden && other.updateWhileHidden);
    setUpdateWhileHidden(false);
    check("global reaches all windows", !ctx.updateWhileHidden && !other.updateWhileHidden);
    second.window->setUpdateWhileHidden(true);
    check("local override targets only receiver", !ctx.updateWhileHidden && other.updateWhileHidden);
    Window inheritsFalse;
    check("local override leaves global default", !internal::WindowRequestAccess::context(inheritsFalse).updateWhileHidden);
    setUpdateWhileHidden(true);
    check("later global overrides local", ctx.updateWhileHidden && other.updateWhileHidden);
    Window inheritsTrue;
    check("new window inherits true", internal::WindowRequestAccess::context(inheritsTrue).updateWhileHidden);
    hidden(window, true);
    window.setFps(VSYNC);
    tick(window, 1, true);
    tick(window, 2, true);
    check("default true updates while hidden", ctx.updateCount == 2 && ctx.drawCount == 0);
    window.setUpdateWhileHidden(false);
    window.setIndependentFps(4, VSYNC);
    tick(window, 3, true);
    tick(window, 3600, true);
    check("false pauses hidden updates", ctx.updateCount == 2);
    hidden(window, false);
    tick(window, 7200, false);
    check("resume has one step without catch-up burst", ctx.updateCount == 3 && ctx.drawCount == 1);
    hidden(window, true);
    window.setDrawWhileHidden(true);
    tick(window, 7201, false);
    tick(window, 7202, false);
    check("draw true/update false draws without updates", ctx.updateCount == 3 && ctx.drawCount == 3);
    window.setIndependentFps(4, 2);
    tick(window, 7203, false);
    const auto throttledDraws = ctx.drawCount;
    tick(window, 7203, false);
    check("paused updates retain fixed draw pacing", ctx.drawCount == throttledDraws);
    window.setUpdateWhileHidden(true);
    tick(window, 10800, false);
    check("enabling while hidden resets paused interval", ctx.updateCount == 4);
    check("update setter leaves draw policy intact", ctx.drawWhileHidden);
    window.setDrawWhileHidden(false);
    check("draw setter leaves update policy intact", ctx.updateWhileHidden);
    struct PendingReadback : internal::BufferReadbackRequest {
        int& polls;
        explicit PendingReadback(int& count) : polls(count) {}
        bool poll() override { ++polls; return false; }
    };
    int polls = 0;
    internal::bufferReadbacks().enqueue(std::make_unique<PendingReadback>(polls));
    internal::backgroundUpdateOnly() = true;
    tick(window, 10801, true);
    internal::backgroundUpdateOnly() = false;
    check("CPU-only hidden tick defers GPU readbacks", polls == 0);
    tick(window, 10802, true);
    check("ordinary ticks resume GPU readback polling", polls > 0);
    internal::bufferReadbacks().shutdown();
    internal::releaseRuntimeWindow(window);
    internal::releaseRuntimeWindow(*second.window);
    return failures ? 1 : 0;
}
