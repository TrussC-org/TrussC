// #239 Q2 / #307 / #984: real shared tick, deterministic native times and
// presentation and App-priority callbacks. Unit draws omit Node transforms;
// display mode exercises the production App draw binding. No sleeps.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
using namespace tc;
namespace {
using Clock = std::chrono::steady_clock;
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("windowTick: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
Clock::time_point at(double seconds) {
    static const auto origin = Clock::now() + std::chrono::hours(24);
    return origin
        + std::chrono::round<Clock::duration>(std::chrono::duration<double>(seconds));
}
Clock::time_point fakeNow;
Clock::time_point readFakeClock() { return fakeNow; }
uint64_t nativeStandIn = 0;
struct Secondary : Window {
    Secondary() { native_ = &nativeStandIn; internal::WindowRequestAccess::bindForTests(*this, 202); }
    ~Secondary() { native_ = nullptr; internal::WindowRequestAccess::unbind(*this); internal::WindowRequestAccess::endApp(*this); }
};
struct Probe : App {
    int updates = 0, draws = 0;
    void update() override { ++updates; }
    void draw() override { ++draws; }
};
void independentTargets(Window& main) {
    auto* previous = internal::currentWindowCtx();
    Secondary secondary;
    secondary.setFps(12);
    internal::currentWindowCtx() = &secondary.context();
    main.setIndependentFps(30, VSYNC);
    check("Window::setIndependentFps targets main from secondary context",
        main.context().loop.updateTargetFps == 30 && main.context().loop.drawTargetFps == VSYNC &&
        !main.context().loop.updateSyncedToDraw);
    check("main independent setter leaves secondary untouched",
        secondary.context().loop.updateTargetFps == 12 && secondary.context().loop.drawTargetFps == 12 &&
        secondary.context().loop.updateSyncedToDraw);
    check("main independent setter restores secondary context", internal::currentWindowCtx() == &secondary.context());
    internal::currentWindowCtx() = &main.context();
    secondary.setIndependentFps(24, 8);
    check("Window::setIndependentFps targets secondary from main context",
        secondary.context().loop.updateTargetFps == 24 && secondary.context().loop.drawTargetFps == 8 &&
        !secondary.context().loop.updateSyncedToDraw);
    check("secondary independent setter leaves main untouched",
        main.context().loop.updateTargetFps == 30 && main.context().loop.drawTargetFps == VSYNC &&
        !main.context().loop.updateSyncedToDraw);
    check("secondary independent setter restores main context", internal::currentWindowCtx() == &main.context());
    internal::currentWindowCtx() = nullptr;
    secondary.setIndependentFps(15, EVENT_DRIVEN);
    check("independent setter restores null context", internal::currentWindowCtx() == nullptr);
    check("independent setter targets secondary outside callbacks",
        secondary.context().loop.updateTargetFps == 15 && secondary.context().loop.drawTargetFps == EVENT_DRIVEN &&
        main.context().loop.updateTargetFps == 30 && main.context().loop.drawTargetFps == VSYNC);
    internal::currentWindowCtx() = previous;
}
void setUnitApp(Window& window, std::shared_ptr<App> app) {
    window.setApp(std::move(app));
    internal::applyPendingAppForTests(window);
    // Window::prepareAppEvents installs update, draw, then the other App
    // bindings. Keep its update/timer path, but replace the draw slot: even
    // an empty App::draw() goes through Node::drawTree() and touches sgl,
    // which this deterministic fixture deliberately does not initialize.
    // Resolve the current App at delivery so swaps retain the same slot.
    window.context().appEventListeners.at(1) = internal::FrameworkEventAccess::listen(
        window.events().draw, [&window] {
            if (auto app = window.getApp()) {
                internal::setupNodeOnce(*app);
                app->draw();
            }
        }, EventPriority::App);
}
const internal::WindowTickPresentation fakePresentation{[] {}, [] { return true; }, [] {}};
bool tick(Window& window, double seconds, bool force = false) {
    fakeNow = at(seconds);
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window.context();
    bool result;
    { internal::WindowDispatchScope scope(window);
      result = internal::runWindowTick(at(seconds), 0.25, force, &fakePresentation); }
    internal::currentWindowCtx() = previous;
    return result;
}
void nativeGatedTicks(Window& window) {
    auto* previous = internal::currentWindowCtx();
    auto& ctx = window.context();
    internal::currentWindowCtx() = &ctx;
    const auto updates = ctx.updateCount, draws = ctx.drawCount;
    ctx.loop = {};
    setFps(VSYNC);
    internal::runWindowTick(at(0), .25, false, &fakePresentation, true);
    internal::runWindowTick(at(1), .25, true, &fakePresentation, true);
    check("native gated callback retains secondary pause including capture",
        ctx.isMain || ctx.updateCount == updates);
    check("native gate keeps main decision B", !ctx.isMain || ctx.updateCount == updates + 2);
    check("native gate never completes a draw", ctx.drawCount == draws);
    // Leave the regular fixture's counters and phase untouched.
    ctx.updateCount = updates; ctx.drawCount = draws;
    ctx.visibilityPaused = false; ctx.mainUpdateCallTimeInitialized = false;
    ctx.rateIndex = ctx.rateCount = 0;
    ctx.frameUptimeSampled = false; ctx.tickDuration = 0;
    internal::currentWindowCtx() = previous;
}

// The key is a compatibility rule, independent of GPU completion and draw success.
void publishHidden(Window& window, bool hidden) {
    auto metadata = internal::WindowRequestAccess::binding(window).metadata;
    metadata.trust_hidden_signals = true; metadata.iconified = false; metadata.occluded = hidden;
    internal::publishWindowMetadata(window, metadata);
}
void resourceKeyPlacement(Window& main) {
    Secondary owner, other;
    setUnitApp(main, std::make_shared<Probe>());
    setUnitApp(owner, std::make_shared<Probe>());
    setUnitApp(other, std::make_shared<Probe>());
    for (auto* window : {&main, static_cast<Window*>(&owner)}) {
        auto& ctx = window->context();
        internal::currentWindowCtx() = &ctx;
        uint64_t expected = internal::resourceFrameKey();
        bool callbackPlacement = true;
        auto observe = [&] {
            callbackPlacement &= internal::resourceFrameKey() ==
                (ctx.isMain ? getUpdateCount() : expected);
        };
        auto update = events().update.listen(observe);
        auto draw = events().draw.listen(observe);
        double seconds = 9000;
        auto attempt = [&](const char* label, bool force, bool rejected, bool gated, bool skipAdmission = false) {
            const auto before = internal::resourceFrameKey();
            const auto foreign = other.context().resourceTickCount;
            expected = before + (!ctx.isMain && !gated && !skipAdmission ? 1 : 0);
            const internal::WindowTickPresentation presentation{[] {}, [=] { return !rejected; }, [] {}};
            fakeNow = at(seconds++);
            internal::runWindowTick(fakeNow, .25, force, &presentation, gated);
            check(label, internal::resourceFrameKey() == (ctx.isMain ? ctx.updateCount : expected));
            check("resource key is placed before update/draw listeners", callbackPlacement);
            check("owner tick leaves another secondary resource key unchanged", other.context().resourceTickCount == foreign);
            const auto saved = internal::resourceFrameKey();
            tick(other, seconds);
            check("another window tick leaves selected resource key unchanged", internal::resourceFrameKey() == saved);
        };
        setFps(VSYNC);
        attempt("resource key: synchronized tick", false, false, false);
        setFps(.25f);
        attempt("resource key: synchronized throttle admits first tick", false, false, false);
        attempt("resource key: synchronized throttle skips before increment", false, false, false, true);
        setIndependentFps(VSYNC, EVENT_DRIVEN); ctx.loop.redrawCount = 0;
        const auto updates = ctx.updateCount, draws = ctx.drawCount;
        attempt("resource key: update-only admitted tick", false, false, false);
        check("key placement fixture updated without drawing", ctx.updateCount == updates + 1 && ctx.drawCount == draws);
        setIndependentFps(EVENT_DRIVEN, VSYNC);
        attempt("resource key: draw-only admitted tick", false, false, false);
        check("key placement fixture drew without updating", ctx.updateCount == updates + 1 && ctx.drawCount == draws + 1);
        attempt("resource key: failed render admission is still an admitted tick", false, true, false);
        setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN); ctx.loop.redrawCount = 0;
        attempt("resource key: idle admitted tick", false, false, false);
        setFps(EVENT_DRIVEN); ctx.loop.redrawCount = 0;
        attempt("resource key: synchronized idle callback has no admission", false, false, false, true);
        setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN);
        publishHidden(*window, true);
        attempt("resource key: hidden capture admitted tick", true, false, false);
        attempt("resource key: paused secondary tick", false, false, false, !ctx.isMain);
        attempt("resource key: native gate including capture never advances secondary", true, false, true);
        publishHidden(*window, false);
        setIndependentFps(4, EVENT_DRIVEN); ctx.loop.redrawCount = 0;
        attempt("resource key: initial fixed update tick", false, false, false);
        const auto catchupUpdates = ctx.updateCount;
        attempt("resource key: multiple catch-up updates use one secondary tick", false, false, false);
        check("key placement fixture ran several catch-up updates", ctx.updateCount > catchupUpdates + 1);
        auto switchMode = events().update.listen([] { setFps(VSYNC); });
        attempt("resource key: update-time mode switch admits exactly once", false, false, false);
        internal::currentWindowCtx() = nullptr;
    }
}

void exercise(Window& window) {
    auto* previous = internal::currentWindowCtx();
    auto& ctx = window.context();
    internal::currentWindowCtx() = &ctx;
    ctx.loop = {};
    auto app = std::make_shared<Probe>();
    setUnitApp(window, app);
    setIndependentFps(4, EVENT_DRIVEN);
    check("independent rates target current window", ctx.loop.updateTargetFps == 4 && ctx.loop.drawTargetFps == 0 && !ctx.loop.updateSyncedToDraw);
    check("getFpsSettings reads current window", getFpsSettings().updateFps == 4 && !getFpsSettings().synced);
    int beforeTimer = 0, afterTimer = 0, drawTimer = 0;
    bool updateScope = true, drawScope = false;
    double frameSample = -1;
    auto before = events().update.listen([&] {
        updateScope &= internal::isInUpdate() && internal::isFixedStepUpdate() && !ctx.inCallbackClock;
        frameSample = getFrameElapsedTime();
        if (getUpdateCount() == 1) app->callAfter(1, [&] { ++beforeTimer; });
    });
    auto after = events().update.listen([&] {
        updateScope &= internal::isInUpdate();
        if (getUpdateCount() == 1) app->callAfter(1, [&] { ++afterTimer; });
    }, EventPriority::AfterApp);
    auto draw = events().draw.listen([&] {
        drawScope = internal::isInUpdate() && !internal::isFixedStepUpdate();
        check("frame sample shared by update and draw", frameSample == getFrameElapsedTime());
        if (app->draws == 0) app->callAfter(1, [&] { ++drawTimer; });
    });
    tick(window, 0);
    for (int i = 1; i <= 3; ++i) tick(window, i * .25);
    check("timers do not fire early", beforeTimer == 0 && afterTimer == 0 && drawTimer == 0);
    tick(window, 1);
    check("#307 before/after App timers fire on fourth step", beforeTimer == 1 && afterTimer == 1);
    check("draw timer starts at creation", drawTimer == 1);
    check("all update and draw listeners have clock scope", updateScope && drawScope && !ctx.inUpdate);
    check("one initial real draw during independent updates", getDrawCount() == 1 && app->draws == 1 && getUpdateCount() == 5);
    check("frame count equals update count", getFrameCount() == getUpdateCount());
    const int rateIndex = ctx.rateIndex;
    const double fps = getFrameRate();
    bool readOnly = true;
    for (int i = 0; i < 100; ++i) readOnly &= getFrameRate() == fps && ctx.rateIndex == rateIndex;
    check("FPS reads are read-only", readOnly);
    check("FPS measured from steps", std::abs(fps - 4) < 1e-6);
    redraw(3);
    for (int i = 5; i <= 9; ++i) tick(window, i * .25);
    check("N redraws give N real draws", getDrawCount() == 4 && app->draws == 4);
    int requested = 0;
    auto redrawInsideDraw = events().draw.listen([&] { if (++requested == 1) redraw(); });
    redraw(); tick(window, 2.5); tick(window, 2.75);
    check("redraw from draw survives", requested == 2 && getDrawCount() == 6);
    redrawInsideDraw.disconnect();
    before.disconnect(); after.disconnect(); draw.disconnect();
    window.setFps(2);
    const auto updates = getUpdateCount(), draws = getDrawCount();
    for (int i = 0; i < 8; ++i) tick(window, 3 + i * .25);
    check("Window::setFps drives this scheduler", getUpdateCount() - updates == 4 && getDrawCount() - draws == 4 && window.getFps() == 2);
    setIndependentFps(4, EVENT_DRIVEN);
    tick(window, 5);
    publishHidden(window, true);
    auto count = getUpdateCount();
    if (ctx.isMain) {
        tick(window, 5.25);
        check("hidden main keeps updating", getUpdateCount() == count + 1);
        publishHidden(window, false);
        setFps(VSYNC); setIndependentFps(4, EVENT_DRIVEN); tick(window, 7205);
    } else {
        tick(window, 3605);
        check("hidden secondary retains native pause policy", getUpdateCount() == count);
        publishHidden(window, false); tick(window, 7205);
        check("resume does not replay hidden time", getUpdateCount() == count + 1);
    }
    setFps(VSYNC); setIndependentFps(4, EVENT_DRIVEN);
    publishHidden(window, true); internal::observeWindowVisibility(ctx, SAPP_EVENTTYPE_SUSPENDED);
    const auto captureDraws = getDrawCount(), captureUpdates = getUpdateCount();
    check("hidden capture still renders and serves afterFrame", tick(window, 7205.05, true) && getDrawCount() == captureDraws + 1);
    tick(window, 7205.1, true);
    check("hidden forced frames preserve fixed update pacing", getUpdateCount() == captureUpdates + 1);
    publishHidden(window, false);
    setFps(VSYNC); setIndependentFps(4, EVENT_DRIVEN); tick(window, 7205.2);
    int eventTimer = 0;
    fakeNow = at(7205.2); ctx.tickTime = fakeNow; ctx.tickActive = true;
    { internal::WindowCallbackTimeScope eventScope;
      check("event scope uses window clock", internal::isInUpdate() && !internal::isFixedStepUpdate());
      app->callAfter(.5, [&] { ++eventTimer; }); }
    ctx.tickActive = false;
    tick(window, 7205.45); check("event timer not early", eventTimer == 0);
    tick(window, 7205.7); check("event timer on time", eventTimer == 1);
    setFps(EVENT_DRIVEN);
    ctx.loop.redrawCount = 0;
    const auto idleDraws = getDrawCount(), idleUpdates = getUpdateCount();
    tick(window, 7206); tick(window, 7206.25);
    check("synchronized EVENT_DRIVEN stays idle without redraw", getDrawCount() == idleDraws && getUpdateCount() == idleUpdates);
    redraw(3);
    for (int i = 0; i < 5; ++i) tick(window, 7206.5 + i * .25);
    check("synchronized N redraws give N draws and updates", getDrawCount() == idleDraws + 3 && getUpdateCount() == idleUpdates + 3);
    setIndependentFps(EVENT_DRIVEN, EVENT_DRIVEN);
    ctx.mainUpdateCallTime = at(3608); // an hour of the previous mode
    ctx.mainUpdateCallTimeInitialized = true;
    auto switchFromDraw = events().draw.listen([] { setFps(VSYNC); });
    redraw(); tick(window, 7208);
    switchFromDraw.disconnect();
    tick(window, 7208.25);
    check("mode change from draw excludes the old mode's time", getDeltaTime() == .25);
    setIndependentFps(4, VSYNC);
    const auto beforeSwitch = getUpdateCount();
    auto switchMode = events().update.listen([] { setFps(VSYNC); });
    tick(window, 7209);
    switchMode.disconnect();
    check("independent to synchronized switch updates once in its tick", getUpdateCount() == beforeSwitch + 1);
    setFps(EVENT_DRIVEN); ctx.loop.redrawCount = 0;
    redraw();
    const auto beforeFailedDraw = getDrawCount();
    int completed = 0;
    auto completion = events().afterFrame.listen([&] { ++completed; });
    const internal::WindowTickPresentation failedPresent{[] {}, [] { return false; }, [] {}};
    const bool admitted = internal::runWindowTick(at(7210), .25, false, &failedPresent);
    check("non-throwing skipped present retains redraw without completing a draw", !admitted && getDrawCount() == beforeFailedDraw && ctx.loop.redrawCount == 1 && completed == 0 && !ctx.tickActive && !ctx.inUpdate && !ctx.inCallbackClock);
    tick(window, 7210.25);
    check("skipped present retries the same redraw", getDrawCount() == beforeFailedDraw + 1 && ctx.loop.redrawCount == 0 && completed == 1);
    completion.disconnect();
    auto nestedUpdate = events().update.listen([&] {
        check("a tick nested in an event uses an update timing scope", ctx.inUpdate && !ctx.inCallbackClock);
    });
    {
        internal::WindowCallbackTimeScope eventScope;
        const auto eventTime = internal::getUpdateTime();
        setIndependentFps(4, VSYNC); tick(window, 7211);
        check("nested tick restores the event timing scope", ctx.inUpdate && ctx.inCallbackClock && !ctx.fixedStepUpdate && internal::getUpdateTime() == eventTime);
    }
    check("event timing scope restores its caller", !ctx.inUpdate && !ctx.inCallbackClock);
    internal::currentWindowCtx() = previous;
}
std::shared_ptr<Window> displaySecondary;
bool displayDone = false, hideMain = false;
bool readVisibility(sapp_window handle, void* user, sapp_tc_window_metadata& metadata) {
    auto& w = *static_cast<Window*>(user);
    if (!sapp_tc_window_read_metadata(handle, &metadata)) metadata = internal::WindowRequestAccess::binding(w).metadata;
    metadata.trust_hidden_signals = true;
    metadata.occluded = &w == internal::mainWindow() && hideMain;
    return true;
}
const internal::WindowNativeOperations& visibilityOperations() {
    static const auto operations = [] {
        auto ops = internal::productionWindowOperations(); ops.readMetadata = readVisibility; return ops;
    }();
    return operations;
}
void setMainHidden(bool hidden) {
    hideMain = hidden;
    auto& w = *internal::mainWindow();
    auto& b = internal::WindowRequestAccess::binding(w);
    b.operations = &visibilityOperations(); b.user = &w;
    internal::refreshWindowMetadata(w);
}
void hiddenInterval(Window& main) {
    Secondary visible;
    setUnitApp(main, std::make_shared<Probe>());
    setUnitApp(visible, std::make_shared<Probe>());
    internal::currentWindowCtx() = &main.context();
    setIndependentFps(4, EVENT_DRIVEN); tick(main, 8000);
    visible.setFps(4); tick(visible, 8000);
    setMainHidden(true);
    const auto updates = main.context().updateCount, draws = main.context().drawCount;
    const auto visibleDraws = visible.context().drawCount;
    for (int i = 1; i <= 8; ++i) {
        tick(main, 8000 + i * .25); tick(visible, 8000 + i * .25);
    }
    check("hidden interval: main updates advance", main.context().updateCount == updates + 8);
    check("hidden interval: idle EVENT_DRIVEN main skips draws", main.context().drawCount == draws);
    check("hidden interval: visible window draws normally", visible.context().drawCount == visibleDraws + 8);
    // Synchronized main also retains configured updates while hidden. Draws
    // are paced by the configured rate and native tick source, as on HEAD.
    main.setFps(2);
    const auto syncedUpdates = main.context().updateCount, syncedDraws = main.context().drawCount;
    for (int i = 0; i < 8; ++i) tick(main, 8003 + i * .25);
    check("hidden synced main keeps configured pacing", main.context().updateCount == syncedUpdates + 4 && main.context().drawCount == syncedDraws + 4);
    setMainHidden(false);
    internal::currentWindowCtx() = nullptr;
}
#ifndef __EMSCRIPTEN__
void captureExpiryWithoutPresentation(Window& main) {
    using namespace mcp::internal;
    internal::currentWindowCtx() = &main.context();
    setFps(EVENT_DRIVEN);
    int afterFrames = 0, produced = 0;
    auto completion = events().afterFrame.listen([&] {
        ++afterFrames;
        drainDeferredResponses(); // same successful-frame drain as the runner
    });
    const internal::WindowTickPresentation rejected{[] {}, [] { return false; }, [] {}};
    const auto draws = getDrawCount();
    const auto start = Clock::now();
    auto attempt = [](Clock::time_point now, double duration, bool force,
                      const internal::WindowTickPresentation* presentation) {
        fakeNow = now;
        return internal::runWindowTick(now, duration, force, presentation);
    };
    Secondary target;
    auto enqueue = [&](const void* window, Clock::time_point deadline, int id) {
        DeferredResponse entry;
        entry.id = id;
        entry.target = window;
        entry.deadline = deadline;
        entry.response = std::make_shared<std::promise<ReplyThunk>>();
        auto future = entry.response->get_future();
        entry.makeEnvelope = [&] { ++produced; return [] { return std::string("captured"); }; };
        std::lock_guard<std::mutex> lock(deferredResponsesMutex());
        deferredResponses().push_back(std::move(entry));
        return future;
    };
    auto ready = [](auto& future) { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    auto timedOut = [&](auto& future, int id) {
        if (!ready(future)) return false; // never block the regression on a lost drain
        const auto reply = Json::parse(future.get()(), nullptr, false);
        return reply.is_object() && reply["id"] == id && reply["result"].value("isError", false)
            && reply.dump().find("the window rendered no frame within 5 s") != std::string::npos;
    };
    auto expired = enqueue(&target.context(), start - std::chrono::seconds(1), 239);
    for (int i = 0; i < 8; ++i)
        attempt(start + std::chrono::milliseconds(100 * i), .1, true, &rejected);
    check("expired targeted reply completes through repeated failed admissions", timedOut(expired, 239) && produced == 0 && !hasDeferredResponses());
    check("persistent rejection emits no afterFrame and counts no draw", afterFrames == 0 && getDrawCount() == draws);

    // Dispatch the real main screenshot tool through the production queue.
    // Advance the supplied steady tick times to its actual stored deadline;
    // no sleep, shortened timeout, GPU readback or successful frame is needed.
    registerStandardTools(Server::instance());
    const auto grant = mcp::getToolLevel();
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::stopHttpServer();
    resetHttpChannel();
    auto promise = std::make_shared<std::promise<ReplyThunk>>();
    auto capture = promise->get_future();
    const Json request = {{"jsonrpc", "2.0"}, {"id", 307}, {"method", "tools/call"},
        {"params", {{"name", "tc_get_screenshot"}, {"arguments", {{"window", 0}}}}}};
    getHttpChannel().send(mcp::McpRequest{request.dump(), promise, std::make_shared<WorkBudget>(true)});
    mcp::processHttpQueue();
    Clock::time_point deadline{};
    {
        std::lock_guard<std::mutex> lock(deferredResponsesMutex());
        check("main screenshot defers with the existing five-second deadline", deferredResponses().size() == 1 && !ready(capture));
        if (!deferredResponses().empty()) {
            auto& entry = deferredResponses().front();
            deadline = entry.deadline;
            auto envelope = std::move(entry.makeEnvelope);
            entry.makeEnvelope = [&, envelope = std::move(envelope)] { ++produced; return envelope(); };
        }
    }
    const auto firstAttempt = deadline - kTargetedDeferralTimeout;
    for (int i = 0; i < 20; ++i)
        attempt(firstAttempt + std::chrono::milliseconds(250 * i), .25, true, &rejected);
    check("main capture retries admission without premature readback or reply", !ready(capture) && hasDeferredResponses() && produced == 0);
    attempt(deadline, .25, true, &rejected);
    check("main capture times out at its deadline without a successful frame", timedOut(capture, 307) && !hasDeferredResponses() && produced == 0);
    check("capture timeout preserves failed-admission draw and afterFrame contract", afterFrames == 0 && getDrawCount() == draws);
    check("capture timeout releases request working credits", workingMemory() == 0);
    mcp::setToolLevel(grant);
    mcp::stopHttpServer();

    auto blocked = enqueue(nullptr, deadline, 987);
    const internal::WindowTickPresentation slowAdmission{[] {}, [] {
        fakeNow += std::chrono::seconds(1); // native admission can block
        return false;
    }, [] {}};
    attempt(deadline - std::chrono::seconds(1), .25, true, &slowAdmission);
    check("admission work counts toward the capture deadline", timedOut(blocked, 987) && produced == 0 && afterFrames == 0 && getDrawCount() == draws);

    auto retry = enqueue(nullptr, deadline + kTargetedDeferralTimeout, 984);
    attempt(deadline, .25, true, &rejected);
    check("unexpired capture remains queued after rejected admission", !ready(retry) && produced == 0);
    attempt(deadline + std::chrono::milliseconds(1), .25, true, &fakePresentation);
    check("capture producer runs once after admission recovers", ready(retry) && retry.get()() == "captured" && produced == 1 && afterFrames == 1);

    auto idle = enqueue(nullptr, deadline, 985);
    main.context().loop.redrawCount = 0;
    attempt(deadline, .25, false, &fakePresentation);
    check("idle non-presenting main tick also expires replies", timedOut(idle, 985) && afterFrames == 1);
    internal::currentWindowCtx() = &target.context();
    target.context().suspended = true;
    auto paused = enqueue(&target.context(), deadline, 986);
    attempt(deadline, .25, false, &rejected);
    check("paused secondary tick also expires its own replies", timedOut(paused, 986) && !hasDeferredResponses());
    internal::currentWindowCtx() = nullptr;
}
#endif
sg_swapchain unavailableSwapchain(void*) {
    sg_swapchain sc{}; sc.invalid = true; return sc;
}
struct SecondaryDisplayApp : App {
    int draws = 0;
    void setup() override { setIndependentFps(VSYNC, EVENT_DRIVEN); redraw(3); }
    void draw() override {
        ++draws;
        check("secondary native draw counter", getDrawCount() + 1 == static_cast<uint64_t>(draws));
        check("secondary native frame/update equality", getFrameCount() == getUpdateCount());
    }
};
void onNextPrelude(std::function<void()> callback) {
#ifndef __EMSCRIPTEN__
    // runOnMainThread executes inline on main; enqueue explicitly so this
    // test exercises the next native tick's prelude, outside any active tick.
    internal::mainThreadQueue().send(std::move(callback));
#else
    runOnMainThread(std::move(callback)); // display mode requires desktop
#endif
}
struct MainDisplayApp : App {
    EventListener after;
    Texture texture;
    Pixels pixels;
    int draws = 0, updates = 0, phase = 0, idleUploads = 0;
    uint64_t firstDevice = 0, hiddenUpdates = 0, hiddenDraws = 0, visibleDraws = 0;
    uint32_t uploadFrame = 0;
    int hiddenUploads = 0, uploadRejections = 0;
    EventListener uploadLog;
    void queueHiddenUpload() {
        onNextPrelude([this] {
            check("prelude upload runs while main hidden", hideMain);
            const auto frame = sg_query_stats().prev_frame.frame_index;
            if (hiddenUploads) check("hidden upload closes GPU frame before next upload", frame != uploadFrame);
            uploadFrame = frame;
            sg_image_data data{}; data.mip_levels[0] = {pixels.getData(), pixels.getTotalBytes()};
            sg_update_image(texture.getImage(), &data);
            if (!hiddenUploads) {
                // Two synthetic hidden ticks with no intervening secondary
                // commit: a visible window must not mask the missing boundary.
                auto& ctx = internal::mainWindowContext();
                const auto loop = ctx.loop;
                ctx.loop.updateTargetFps = EVENT_DRIVEN;
                ctx.loop.redrawCount = 0;
                const auto now = std::chrono::steady_clock::now();
                check("hidden upload-only tick does not present", !internal::runWindowTick(now, 1.0 / 60));
                check("hidden upload-only tick completes GPU work", sg_query_stats().prev_frame.frame_index != frame);
                sg_update_image(texture.getImage(), &data);
                internal::runWindowTick(now + std::chrono::milliseconds(17), 1.0 / 60);
                ctx.loop = loop;
            }
            ++hiddenUploads;
            if (hiddenUploads < 2) queueHiddenUpload();
        });
    }
    void setup() override {
        setIndependentFps(VSYNC, EVENT_DRIVEN);
        getWindow()->setClearColor(Color(1, 0, 0, 1));
        pixels.allocate(4, 4, 4);
        texture.allocate(4, 4, 4, TextureUsage::Stream);
        WindowSettings settings; settings.width = settings.height = 64;
        displaySecondary = createWindow(settings);
        check("native secondary created", bool(displaySecondary));
        if (displaySecondary) displaySecondary->setApp(std::make_shared<SecondaryDisplayApp>());
        after = events().afterFrame.listen([this] {
            if (draws <= 2) {
                Pixels screen;
                const bool captured = grabScreen(screen) && screen.isAllocated();
                check("main clear capture", captured);
                if (captured) {
                    const auto c = screen.getColor(0, 0);
                    check(draws == 1 ? "main setClearColor reaches presentation" : "explicit clear overrides window color",
                          draws == 1 ? c.r > .9f && c.b < .1f : c.b > .9f && c.r < .1f);
                }
            }
        });
    }
    void update() override {
        ++updates;
        check("native main frame/update equality", getFrameCount() == getUpdateCount());
        if (phase == 0 && draws == 1) {
            int warnings = 0;
            auto logs = getLogger().onLog.listen([&](LogEventArgs& e) {
                if (e.message.find("loadData() called twice") != std::string::npos) ++warnings;
            });
            check("internal device advances while draw counter idle", internal::deviceFrameCount() != firstDevice && getDrawCount() == 1);
            texture.loadData(pixels);
            check("next device frame accepts upload without a draw", warnings == 0);
            texture.loadData(pixels);
            check("internal device guard rejects second upload", warnings == 1);
            firstDevice = internal::deviceFrameCount();
            if (++idleUploads == 2) { phase = 1; redraw(); }
        } else if (phase == 1 && draws == 2) {
            phase = 2;
            onNextPrelude([this] {
                setMainHidden(true);
                check("main visibility reports scheduler input", getWindow()->isOccluded());
                hiddenUpdates = getUpdateCount(); hiddenDraws = getDrawCount();
                visibleDraws = displaySecondary->context().drawCount;
                displaySecondary->setFps(VSYNC);
                uploadLog = getLogger().onLog.listen([this](LogEventArgs& e) {
                    if (e.message.find("sg_update_image: only one update allowed per image and frame") != std::string::npos) ++uploadRejections;
                });
                queueHiddenUpload();
            });
        } else if (phase == 2 && hideMain && displaySecondary &&
                   getUpdateCount() >= hiddenUpdates + 8 && displaySecondary->context().drawCount >= visibleDraws + 3 && hiddenUploads == 2) {
            check("display hidden interval: main updates advance", getUpdateCount() > hiddenUpdates);
            check("display hidden interval: idle main draws skipped", getDrawCount() == hiddenDraws);
            check("display hidden interval: visible secondary draws advance", displaySecondary->context().drawCount >= visibleDraws + 3);
            check("hidden prelude uploads avoid VALIDATE_UPDIMG_ONCE", uploadRejections == 0);
            uploadLog.disconnect();
            setMainHidden(false);
            check("main visible again", !getWindow()->isOccluded());
            // Exercise Sokol's actual non-throwing invalid-swapchain path.
            internal::mainWindowContext().acquireSwapchain = unavailableSwapchain;
            redraw(); phase = 3;
        } else if (phase == 3) {
            check("invalid native swapchain does not count a completed draw", getDrawCount() == 2 && internal::mainLoop().redrawCount == 1);
            internal::mainWindowContext().acquireSwapchain = nullptr;
            phase = 4;
        } else if (phase == 4 && getDrawCount() == 3) {
            check("valid native swapchain retries pending redraw", internal::mainLoop().redrawCount == 0);
            displayDone = true;
            exitApp();
        }
        if (updates > 300) { check("native test reached all phases", false); exitApp(1); }
    }
    void draw() override {
        ++draws;
        if (draws == 1) { texture.loadData(pixels); firstDevice = internal::deviceFrameCount(); }
        if (draws == 2) clear(0, 0, 1, 1);
    }
    void cleanup() override { displaySecondary.reset(); setMainHidden(false); }
};
struct HeadlessProbe : App {
    void update() override {
        check("headless draw count zero", getDrawCount() == 0);
        check("headless frame equals update", getFrameCount() == getUpdateCount());
        requestExit();
    }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    getMainThreadId();
    if (argc > 1 && std::string(argv[1]) == "--display") {
        if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: windowTick display requires desktop secondary windows"); return 0; }
        WindowSettings settings; settings.width = settings.height = 128;
        // runapp-verdict: cleanup-verdict -- final checks propagate failures.
        return tcCoreTest::runDisplayTest<MainDisplayApp>(settings, failures, [] {
            check("display test completed", displayDone);
        });
    }
    internal::createMainWindow(WindowSettings{});
    auto& main = *internal::mainWindow();
    internal::WindowRequestAccess::bindForTests(main, 101);
    internal::windowClockForTests() = readFakeClock;
    independentTargets(main);
    nativeGatedTicks(main);
    exercise(main);
    hiddenInterval(main);
    const auto mainLoop = main.context().loop;
    {
        Secondary second;
        nativeGatedTicks(second);
        exercise(second);
        check("secondary APIs leave main scheduler alone", main.context().loop.updateTargetFps == mainLoop.updateTargetFps && main.context().loop.redrawCount == mainLoop.redrawCount);
    }
    const auto savedUpdates = main.context().updateCount;
    const auto savedDraws = main.context().drawCount;
    main.setApp(std::make_shared<Probe>());
    internal::applyPendingAppForTests(main);
    check("App swap retains host scheduler and counters", main.context().updateCount == savedUpdates && main.context().drawCount == savedDraws && main.context().loop.updateTargetFps == mainLoop.updateTargetFps);
    {
        auto throwing = events().rawEvent.listen([](const sapp_event& event) {
            if (event.type == SAPP_EVENTTYPE_SUSPENDED) throw std::runtime_error("visibility listener probe");
        });
        sapp_event event{}; event.type = SAPP_EVENTTYPE_SUSPENDED;
        setMainHidden(true);
        internal::_event_cb(&event);
        check("suspension survives a throwing raw listener", main.context().suspended && !main.context().inUpdate && !main.context().inCallbackClock);
        throwing.disconnect(); event.type = SAPP_EVENTTYPE_RESUMED; setMainHidden(false);
        internal::_event_cb(&event);
        check("resume clears suspension after listener exception", !main.context().suspended);
    }
#ifndef __EMSCRIPTEN__
    captureExpiryWithoutPresentation(main);
#endif
    resourceKeyPlacement(main);
    internal::destroyMainWindow();
    {
        struct LongSetup : App {
            int fired = 0;
            void setup() override { fakeNow += std::chrono::seconds(3); callAfter(2, [this] { ++fired; }); }
        };
        Secondary window;
        auto app = std::make_shared<LongSetup>(); setUnitApp(window, app);
        window.setFps(VSYNC);
        tick(window, 100);
        tick(window, 103.25);
        check("long setup timer excludes pre-creation delta", app->fired == 0);
        tick(window, 104.75);
        check("long setup timer still not early", app->fired == 0);
        tick(window, 105);
        check("long setup timer on time", app->fired == 1);
    }
    {
        Secondary window;
        auto app = std::make_shared<Probe>(); setUnitApp(window, app);
        internal::currentWindowCtx() = &window.context();
        setIndependentFps(4, EVENT_DRIVEN);
        bool fired = false;
        auto listener = events().update.listen([&] {
            if (getUpdateCount() == 2) app->callAfter(.25, [&] { fired = true; });
        });
        tick(window, 200);
        tick(window, 201); // four fixed steps share one native tick
        check("#307 secondary listener timer fires in next catch-up step", fired);
        internal::currentWindowCtx() = nullptr;
    }
    internal::windowClockForTests() = nullptr;
    // Both the getter and resize production paths call this shared helper.
    for (float dpi : {2.0f, 1.25f, 1.5f, 1.75f}) {
        const int expectedWidth = static_cast<int>(1286 / dpi);
        const int expectedHeight = static_cast<int>(967 / dpi);
        internal::WindowRequestAccess::HostState ctxState; internal::WindowContext& ctx = ctxState.context; ctx.isMain = false;
        ctx.fbWidth = 1286; ctx.fbHeight = 967; ctx.dpiScale = dpi;
        internal::currentWindowCtx() = &ctx;
        const auto getterWidth = getWindowWidth(), getterHeight = getWindowHeight();
        const auto event = internal::framebufferResizeArgs(1286, 967, dpi);
        check("#984 fractional DPI getter and resize agree", event.width == getterWidth && event.height == getterHeight && getterWidth == expectedWidth && getterHeight == expectedHeight);
        internal::currentWindowCtx() = nullptr;
    }
    check("#984 200 percent truncates 1286x967 to 643x483", internal::framebufferToLogical(1286, 2) == 643 && internal::framebufferToLogical(967, 2) == 483);
    internal::mainWindowContext().drawCount = 0;
    runHeadlessApp<HeadlessProbe>();
    return failures ? 1 : 0;
}
