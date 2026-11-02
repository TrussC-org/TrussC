#include "../../common/tcWindowTest.h"
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include "resizeDeliveryChecks.h"
#include "windowInternalAccessChecks.h"
using namespace tc;
namespace {
int checks = 0, failures = 0;
const auto fakeTime = std::chrono::steady_clock::time_point{} + std::chrono::seconds(42);
auto clockNow() { return fakeTime; }
void check(const char* name, bool ok) {
    ++checks;
    if (!ok) { ++failures; std::printf("windowState: FAIL: %s\n", name); }
}
struct Native {
    sapp_tc_window_metadata metadata{};
    uint32_t id = 0;
    int queries = 0, acquisitions = 0, activations = 0;
};
bool read(sapp_window h, void* user, sapp_tc_window_metadata& out) {
    auto& n = *static_cast<Native*>(user); ++n.queries;
    check("metadata addresses actual positive native ID", h.id == n.id && h.id != 0);
    out = n.metadata; return true;
}
sg_swapchain acquire(sapp_window h, void* user) {
    auto& n = *static_cast<Native*>(user); ++n.acquisitions;
    check("drawable operation addresses its binding", h.id == n.id); return {};
}
void activate(sapp_window h, void* user) {
    auto& n = *static_cast<Native*>(user); ++n.activations;
    check("activation operation addresses its binding", h.id == n.id);
}
const internal::WindowNativeOperations operations{read, acquire, activate};
struct HeadlessProbe : App {
    void setup() override {
        check("headless has zero windows and a non-window fallback", internal::openWindows().empty() && !internal::currentWindow() && !getWindow() && &internal::currentWindowContext() == &internal::nonWindowContext() && internal::currentWindowContext().render && internal::currentWindowContext().coreEvents);
        check("headless preserves prelaunch event/render references", &events() == internal::nonWindowContext().coreEvents && &internal::getDefaultContext() == &internal::processRenderContext());
        exitApp();
    }
};
struct Probe : App {
    int sizes = 0, setups = 0, updates = 0, exits = 0, cleanups = 0;
    void setup() override { ++setups; }
    void update() override { ++updates; }
    void exit() override { ++exits; }
    void cleanup() override { ++cleanups; }
    bool inspect = true;
    void windowResized(int width, int height) override {
        ++sizes;
        if (inspect) {
            auto* w = getWindow();
            check("App size seed sees bound receiver", w && internal::currentWindow() == w);
            check("fb and DPI published before first App size", w && trussc::internal::WindowRequestAccess::context(*w).fbWidth == 1503 && trussc::internal::WindowRequestAccess::context(*w).fbHeight == 999 && trussc::internal::WindowRequestAccess::context(*w).dpiScale == 1.5f);
            check("initial size uses logical or pixelPerfect units", width == (internal::pixelPerfectMode() ? 1503 : 1002) && height == (internal::pixelPerfectMode() ? 999 : 666));
            check("size callbacks use the fake clock", internal::getUpdateTime() == fakeTime);
            check("metadata seed acquires no drawable", static_cast<Native*>(internal::WindowRequestAccess::binding(*w).user)->acquisitions == 0);
        }
    }
};
void exercise(Window& w, uint32_t id) {
    internal::WindowRequestAccess::resetForLaunch(w);
    check("context pointers are prewired to owned state for both roots", trussc::internal::WindowRequestAccess::context(w).render == &w.render_ && trussc::internal::WindowRequestAccess::context(w).coreEvents == &w.events());
    Native n;
    n.id = id; n.metadata.fb_width = 1503; n.metadata.fb_height = 999;
    n.metadata.dpi_scale = 1.5f; n.metadata.sample_count = 4;
    n.metadata.color_format = SAPP_PIXELFORMAT_RGB10A2;
    n.metadata.depth_format = SAPP_PIXELFORMAT_DEPTH_STENCIL;
    n.metadata.trust_hidden_signals = true;
    check("unbound Window is closed and visible", !w.isOpen() && !w.isOccluded());
    check("zero ID cannot bind", !internal::bindWindow(w, {0}, operations, &n));
    check("same production binding path for both roots", internal::bindWindow(w, {id}, operations, &n));
    check("identity comes from binding", internal::WindowRequestAccess::creationId(w) == id);
    check("actual target format is published before callbacks", trussc::internal::WindowRequestAccess::context(w).swapchainTarget.colorFormat == SG_PIXELFORMAT_RGB10A2 && trussc::internal::WindowRequestAccess::context(w).swapchainTarget.sampleCount == 4);
    check("metadata query needs no native activation", n.activations == 0 && n.acquisitions == 0);
    auto app = internal::makeConstructedApp<Probe>();
    internal::WindowRequestAccess::attachCandidate(w, app); internal::applyPendingAppForTests(w);
    check("initial size delivered once at attachment", app->sizes == 1);
    internal::syncWindowAppSize(w, true); internal::syncWindowAppSize(w);
    check("next entry deduplicates initial size", app->sizes == 1);
    app->inspect = false;
    for (bool trust : {false, true}) for (bool iconified : {false, true}) for (bool occluded : {false, true}) {
        n.metadata.trust_hidden_signals = trust; n.metadata.iconified = iconified; n.metadata.occluded = occluded;
        internal::refreshWindowMetadata(w);
        const bool expected = trust && (iconified || occluded);
        check("effective hidden agrees with ctx and trust", w.isOccluded() == expected && trussc::internal::WindowRequestAccess::context(w).suspended == expected);
        const int queries = n.queries;
        (void)w.isOccluded(); (void)w.isOccluded();
        check("getter has no native side effects", n.queries == queries);
    }
    n.metadata.fb_width = n.metadata.fb_height = 0; n.metadata.drawable_available = false;
    internal::refreshWindowMetadata(w); internal::syncWindowAppSize(w);
    check("minimized zeros retain last valid size", trussc::internal::WindowRequestAccess::context(w).fbWidth == 1503 && trussc::internal::WindowRequestAccess::context(w).fbHeight == 999 && app->sizes == 1);
    n.metadata.fb_width = 1503; n.metadata.fb_height = 999; n.metadata.dpi_scale = 3;
    internal::refreshWindowMetadata(w); internal::syncWindowAppSize(w);
    check("DPI-only revision respects coordinate mode", app->getWidth() == (internal::pixelPerfectMode() ? 1503 : 501) && app->getHeight() == (internal::pixelPerfectMode() ? 999 : 333) && app->sizes == (internal::pixelPerfectMode() ? 1 : 2));
    internal::syncWindowAppSize(w);
    check("DPI refresh deduplicates", app->sizes == (internal::pixelPerfectMode() ? 1 : 2));
    auto edit = w.events().windowResized.listen([](ResizeEventArgs& e) { e.width += 7; }, EventPriority::BeforeApp);
    n.metadata.fb_width += 3;
    internal::refreshWindowMetadata(w); internal::syncWindowAppSize(w);
    const auto editedSizes = app->sizes;
    internal::syncWindowAppSize(w);
    check("unchanged native size is deduplicated after listener edits", app->sizes == editedSizes && app->getWidth() != internal::framebufferToLogical(n.metadata.fb_width, internal::appSizeDpi(n.metadata.dpi_scale)));
    edit.disconnect();
    // A listener can put the App at the next real native extent in advance.
    // That must not consume the next observation's CoreEvents notification.
    n.metadata.dpi_scale = 1;
    n.metadata.fb_width = n.metadata.fb_height = 100;
    internal::refreshWindowMetadata(w); internal::syncWindowAppSize(w);
    int notifications = 0;
    auto collision = w.events().windowResized.listen([&](ResizeEventArgs& e) {
        ++notifications; e.width = e.height = 200;
    }, EventPriority::BeforeApp);
    n.metadata.fb_width = n.metadata.fb_height = 150;
    internal::refreshWindowMetadata(w); internal::syncWindowAppSize(w);
    check("resize listener may edit App to a future native extent", notifications == 1 && app->getWidth() == 200 && app->getHeight() == 200);
    n.metadata.fb_width = n.metadata.fb_height = 200;
    internal::refreshWindowMetadata(w); internal::syncWindowAppSize(w);
    check("new observed extent notifies even when App already matches", notifications == 2);
    internal::syncWindowAppSize(w);
    check("colliding extent is still deduplicated after observation", notifications == 2);
    collision.disconnect();
    auto sizeEntry = [&] {
        tcCoreTest::nativeBoundary([&] {
            internal::WindowEntryScope scope(w);
            internal::syncWindowAppSize(w, true);
            internal::syncWindowAppSize(w);
        });
    };
    check("metadata never acquires a drawable", n.acquisitions == 0);
    n.metadata.trust_hidden_signals = false;
    internal::refreshWindowMetadata(w);
    trussc::internal::WindowRequestAccess::context(w).suspended = true; // stale lifecycle state must not override an untrusted native observation
    auto* previous = internal::currentWindowCtx(); internal::currentWindowCtx() = &trussc::internal::WindowRequestAccess::context(w);
    trussc::internal::WindowRequestAccess::context(w).loop = {};
    w.setIndependentFps(4, EVENT_DRIVEN); trussc::internal::WindowRequestAccess::context(w).loop.redrawCount = 0;
    const auto before = trussc::internal::WindowRequestAccess::context(w).updateCount;
    const internal::WindowTickPresentation presentation{[] {}, [] { return true; }, [] {}};
    internal::runWindowTick(fakeTime, .25, false, &presentation);
    check("scheduler reads effective hidden rather than stale suspension", trussc::internal::WindowRequestAccess::context(w).updateCount == before + 1);
    check("both roots run setup once before the first update", app->setups == 1 && app->updates == 1);
    internal::currentWindowCtx() = previous;
    const auto acquisitions = n.acquisitions, activations = n.activations;
    trussc::internal::WindowRequestAccess::context(w).activateNative(trussc::internal::WindowRequestAccess::context(w).acquireSwapchainUser);
    (void)trussc::internal::WindowRequestAccess::context(w).acquireSwapchain(trussc::internal::WindowRequestAccess::context(w).acquireSwapchainUser);
    check("native operations use this window's table and user", n.acquisitions == acquisitions + 1 && n.activations == activations + 1);
    checkResizeDelivery(w, n.metadata, app, sizeEntry, sizeEntry, check);
    internal::WindowRequestAccess::unbind(w);
    check("closed getter and identity clear", !w.isOpen() && !w.isOccluded() && internal::WindowRequestAccess::creationId(w) == 0);
    internal::WindowRequestAccess::endApp(w);
    check("repeat bindings retain exactly-once App exit and cleanup", app->exits == 1 && app->cleanups == 1);
}
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto canonical = internal::prepareLaunchWindow(WindowSettings{});
    auto* context = &trussc::internal::WindowRequestAccess::context(*canonical);
    auto* events = &canonical->events();
    auto* render = &canonical->render_;
    Window secondary;
    check("every Window owns distinct state", &trussc::internal::WindowRequestAccess::context(secondary) != context && &secondary.events() != events && &secondary.render_ != render);
    internal::windowClockForTests() = clockNow;
    for (bool pixels : {false, true}) {
        internal::pixelPerfectMode() = pixels;
        exercise(*canonical, pixels ? 731 : 730); exercise(secondary, pixels ? 913 : 912);
    }
    internal::pixelPerfectMode() = false;
    internal::windowClockForTests() = nullptr;
    internal::shutdownRuntime();
    internal::endFrameworkLifetime();
    check("retained closed Window keeps CPU state", &trussc::internal::WindowRequestAccess::context(*canonical) == context && &canonical->events() == events && &canonical->render_ == render);
    check("headless runner succeeds", runHeadlessApp<HeadlessProbe>() == 0);
    check("headless restores the null process caller", !internal::currentWindowCtx() &&
        &internal::currentWindowContext() == &internal::nonWindowContext());
    std::printf("windowState: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
