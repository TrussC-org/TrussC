#include <TrussC.h>
#include "../../common/tcWindowTest.h"
#include "../../common/tcCoreTest.h"
#include "../../common/tcBackgroundAudio.h"
#include <cstdio>
#include <thread>
using namespace tc;
namespace trussc::internal { void backgroundTickForTests() noexcept; }
namespace {
int failures = 0;
std::chrono::steady_clock::time_point fakeNow;
std::chrono::steady_clock::time_point readClock() { return fakeNow; }
void timeAt(double seconds) {
    fakeNow = std::chrono::steady_clock::time_point{} +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
}
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
    internal::beginRuntimeRun();
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

    // Android has one Window; the production entry resolves that sole receiver.
    internal::WindowRequestAccess::teardown(*second.window);
    internal::releaseRuntimeWindow(*second.window);
    internal::windowClockForTests() = readClock;
    for (bool synced : {false, true}) {
        for (bool global : {false, true}) {
            for (bool reenable : {false, true}) {
                if (synced) window.setFps(VSYNC);
                else window.setIndependentFps(4, VSYNC);
                window.setUpdateWhileHidden(true);
                hidden(window, true);
                timeAt(20000);
                internal::backgroundTickForTests();
                auto disable = window.events().update.listen([&] {
                    if (global) setUpdateWhileHidden(false);
                    else window.setUpdateWhileHidden(false);
                });
                timeAt(20000.25);
                internal::backgroundTickForTests();
                disable.disconnect();
                check("hidden update setter records pause immediately", ctx.visibilityPaused && !ctx.mainUpdateCallTimeInitialized);
                const auto before = ctx.updateCount;
                // Android removes the callback: NO more hidden ticks follow.
                timeAt(23600.25);
                if (reenable) {
                    if (global) setUpdateWhileHidden(true);
                    else window.setUpdateWhileHidden(true);
                }
                hidden(window, false);
                ctx.lifecycleSuspended = false; // Android RESUMED precedes the visible tick.
                const internal::WindowTickPresentation presentation{[] {}, [] { return true; }, [] {}};
                internal::runWindowTick(fakeNow, .016, false, &presentation, false);
                check("no hidden tick after disable: one visible update and normal delta",
                    ctx.updateCount == before + 1 && ctx.updateDeltaTime >= 0 && ctx.updateDeltaTime < 1);
            }
        }
    }

    window.setFps(VSYNC);
    window.setUpdateWhileHidden(true);
    hidden(window, true);
    const auto wav = getTempPath("windowUpdateWhileHidden.wav");
    const auto png = getTempPath("windowUpdateWhileHidden.png");
    check("write local WAV", tcCoreTest::writeBackgroundWav(wav));
    Pixels pixels;
    pixels.allocate(1, 1, 4);
    check("write local PNG", bool(pixels.save(png)));
    SoundBuffer sound;
    Image image;
    int soundLoads = 0, imageLoads = 0, mainWork = 0, externalDeliveries = 0;
    auto soundListener = sound.onLoad.listen([&](LoadResult& result) {
        if (result && sound.samples.size() == 80) ++soundLoads;
    });
    auto imageListener = image.onLoad.listen([&](LoadResult& result) { if (result) ++imageLoads; });
    Event<std::string> external;
    auto mainListener = external.listen([&](std::string&) { ++externalDeliveries; }, Deliver::Main);
    auto delivery = internal::EventSnapshotDelivery<std::string>::bind(external, std::make_shared<internal::NetworkDelivery>());
    image.loadAsync(png); // GPU result first: must not block a later CPU result.
    sound.loadAsync(wav);
    internal::waitForResourceLoadsForTests();
    std::thread([&] {
        runOnMainThread([&] { ++mainWork; });
        std::string payload = "background delivery";
        delivery->dispatch(payload, {}, true);
    }).join();
    for (int i = 0; i < 100; ++i) {
        timeAt(24000 + i * .016);
        internal::backgroundTickForTests();
    }
    check("background-only ticks publish PCM and onLoad", soundLoads == 1 && !sound.isLoading());
    check("background-only ticks deliver queued control and external Main events", mainWork == 1 && externalDeliveries == 1);
    check("background-only ticks defer GPU-backed load", imageLoads == 0 && image.isLoading());
    hidden(window, false);
    ctx.lifecycleSuspended = false;
    internal::beforeHostTurn();
    internal::afterHostTurn();
    check("visible host turn completes GPU-backed load", imageLoads == 1 && !image.isLoading());
    fs::remove(wav);
    fs::remove(png);
    internal::windowClockForTests() = nullptr;
    internal::releaseRuntimeWindow(window);
    return failures ? 1 : 0;
}
