#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

namespace {
thread_local int failAt = -1;
thread_local int attempts = 0;
thread_local bool fired = false, observing = false;
void arm(int nth) { attempts = 0; fired = false; failAt = nth; observing = true; }
void disarm() { observing = false; failAt = -1; }
}
// Count allocations, rather than matching implementation-specific STL sizes.
// This binary owns the hook; the combined core runner never links it.
void* operator new(std::size_t n) {
    if (observing && attempts++ == failAt) {
        fired = true;
        failAt = -1; // Only one allocation fails, including during unwinding.
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace tc;
namespace {
int failures = 0;
void check(const char* name, bool ok, int nth = -1) {
    if (!ok) ++failures;
    std::printf("%s %s (allocation %d)\n", ok ? "PASS" : "FAIL", name, nth);
}
// Faulted target preparation can strand device-owned handles until shutdown.
// Give every dummy attempt a fresh device so later indices never exercise
// exhausted pools instead of the intended allocating restoration path.
struct AttachmentDevice {
#ifdef TC_WINDOW_ATTACH_DUMMY
    static constexpr bool usesGpu = true;
    AttachmentDevice() {
        sg_desc sd{}; sd.pipeline_pool_size = 2048;
        sd.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        sd.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        sd.environment.defaults.sample_count = 1;
        sg_setup(&sd);
        check("attachment dummy device initialized", sg_isvalid());
        sgl_desc_t gd{}; gd.context_pool_size = 8; gd.pipeline_pool_size = 256;
        sgl_setup(&gd);
        auto& main = internal::mainWindowContext();
        main.fbWidth = main.fbHeight = 32;
        main.swapchainTarget.width = main.swapchainTarget.height = 32;
        internal::restoreWindowContext(nullptr);
    }
    ~AttachmentDevice() {
        internal::mainWindowContext().swapchainTarget.resources().release();
        internal::drainPendingGpuDestroys();
        sgl_shutdown();
        sg_shutdown();
    }
#else
    static constexpr bool usesGpu = false;
#endif
};
template<class T> void notify(Event<T>& event) {
    if constexpr (std::is_void_v<T>) event.notify();
    else { T value{}; event.notify(value); }
}

template<class T> void removal() {
    // Include every snapshot/proxy allocation, with framework ownership and without.
    for (bool framework : {false, true}) {
        for (int nth = 0;; ++nth) {
            Event<T> event;
            int calls = 0;
            EventListener victim;
            auto first = event.listen([&](auto&...) {
                arm(nth);
                victim.disconnect();
                disarm();
            }, 0);
            auto callback = [&](auto&...) { ++calls; };
            victim = framework ? internal::FrameworkEventAccess::listen(event, callback, 100)
                               : event.listen(callback, 100);
            notify(event); // Removal must cancel the entry in this active snapshot.
            const bool injected = fired;
            first.disconnect(); // A successful rebuild must sweep the dead entry.
            notify(event);
            check(std::is_void_v<T> ? "void remove: immediate cancellation and sweep"
                                   : "typed remove: immediate cancellation and sweep",
                  !victim && calls == 0 && event.listenerCount() == 0, nth);
            if (!injected) break;
        }
    }
#if !defined(__EMSCRIPTEN__)
    Event<T> event;
    int calls = 0;
    auto victim = event.listen([&](auto&...) { ++calls; }, Deliver::Main);
    std::thread worker([&] { notify(event); });
    worker.join();
    arm(0); victim.disconnect(); disarm();
    internal::drainMainThreadQueue();
    notify(event);
    check(std::is_void_v<T> ? "void remove: queued Main cancellation"
                           : "typed remove: queued Main cancellation", fired && calls == 0);
#endif
}

template<class T> void registration() {
    // Include the allocation of the disconnect callable, after snapshot creation.
    int total = 0;
    {
        Event<T> event;
        arm(-1);
        auto listener = internal::FrameworkEventAccess::listen(event, [](auto&...) {}, 100);
        disarm(); total = attempts;
    }
    for (int nth = 0; nth < total; ++nth) {
        Event<T> event;
        int calls = 0;
        EventListener listener;
        bool threw = false;
        arm(nth);
        try { listener = internal::FrameworkEventAccess::listen(event, [&](auto&...) { ++calls; }, 100); }
        catch (const std::bad_alloc&) { threw = true; }
        disarm();
        notify(event);
        // stable_sort may recover from its optional allocation failing.
        check(std::is_void_v<T> ? "void registration: no orphan after failure"
                               : "typed registration: no orphan after failure",
              fired && (threw ? calls == 0 && event.listenerCount() == 0
                             : listener && calls == 1 && event.listenerCount() == 1), nth);
    }
}

static std::uint64_t nativeStandIn = 0;
struct TestWindow : Window {
    TestWindow() { native_ = &nativeStandIn; }
    void finish() noexcept {
        if (!native_) return;
        internal::TeardownSteps steps;
        std::optional<internal::EntryStackGuard> guard;
        steps.run("exit entry guard", [&] { guard.emplace(internal::AppEntry::Exit); });
        native_ = nullptr;
        ctx_.targetsClosed = true;
        ctx_.teardownTargets();
        steps.run("stand-in GPU release", [&] { ctx_.swapchainTarget.resources().release(); });
        internal::WindowRequestAccess::endApp(*this);
    }
    ~TestWindow() { finish(); }
};
struct TestApp : App {
    int updates = 0, exits = 0, cleanups = 0;
    void update() override { ++updates; }
    void exit() override { ++exits; }
    void cleanup() override { ++cleanups; }
};
bool countsMatch(Window& win, std::size_t count) {
    auto& e = win.events();
    return e.update.listenerCount() == count && e.draw.listenerCount() == count &&
        e.exit.listenerCount() == count && e.keyPressed.listenerCount() == count &&
        e.keyReleased.listenerCount() == count && e.mousePressed.listenerCount() == count &&
        e.mouseReleased.listenerCount() == count && e.mouseMoved.listenerCount() == count &&
        e.mouseDragged.listenerCount() == count && e.mouseScrolled.listenerCount() == count &&
        e.touchPressed.listenerCount() == count && e.touchMoved.listenerCount() == count &&
        e.touchReleased.listenerCount() == count && e.windowResized.listenerCount() == count &&
        e.filesDropped.listenerCount() == count;
}
// Mirror the review: prepare 80x80, release resources but retain metadata,
// then change dimensions. The next restore rebuilds allocating pipeline caches.
void prepareReleasedTarget(Window& win) {
    if (!sg_isvalid()) return;
    auto* previous = internal::currentWindowCtx();
    auto& ctx = win.context();
    ctx.fbWidth = ctx.fbHeight = 80;
    ctx.swapchainTarget.width = ctx.swapchainTarget.height = 80;
    internal::restoreWindowContext(&ctx);
    internal::restoreWindowContext(previous);
    ctx.swapchainTarget.resources().release();
    ctx.fbWidth = ctx.fbHeight = 100;
    ctx.swapchainTarget.width = ctx.swapchainTarget.height = 100;
    check("attachment target prepared, released and resized",
          ctx.swapchainTarget.resources().width == 80 &&
          ctx.swapchainTarget.resources().height == 80 &&
          !ctx.swapchainTarget.resources().context.id);
}
// A native C callback and the dispatch scope's noexcept destructor must both
// contain registration failure. Run the same allocation matrix through them.
extern "C" void swapAtCallbackBoundary(Window* win, const std::shared_ptr<App>* next, int nth) noexcept {
    internal::WindowDispatchScope scope(*win);
    win->setApp(*next);
    arm(nth);
}
void swaps(bool callbackBoundary = false) {
    int errors = 0;
    auto logger = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error) ++errors;
    });
    for (int nth = 0;; ++nth) {
        AttachmentDevice device;
        TestWindow win;
        auto a = std::make_shared<TestApp>(), b = std::make_shared<TestApp>();
        win.setApp(a); internal::applyPendingAppForTests(win);
        prepareReleasedTarget(win);
        const int before = errors;
        bool threw = false;
        if (callbackBoundary) {
            std::shared_ptr<App> next = b;
            swapAtCallbackBoundary(&win, &next, nth);
        } else {
            win.setApp(b);
            arm(nth);
            try { internal::applyPendingAppForTests(win); }
            catch (const std::bad_alloc&) { threw = true; }
        }
        disarm();
        const bool injected = fired;
        auto current = win.getApp();
        const bool bound = current != nullptr;
        check("swap: contained failure, complete binding set, at most one report",
              !threw && countsMatch(win, bound ? 1 : 0) &&
              win.context().appEventListeners.size() == (bound ? 15u : 0u) &&
              errors - before == (current == b ? 0 : 1), nth);
        auto* prev = internal::currentWindowCtx();
        internal::currentWindowCtx() = &win.context();
        win.events().update.notify();
        win.events().update.notify();
        internal::currentWindowCtx() = prev;
        check("swap: B updates once per frame after disconnect fault",
              b->updates == (current == b ? 2 : 0) && a->updates == (current == a ? 2 : 0), nth);
        win.finish(); win.finish();
        check("swap: B exit exactly once, no stale framework entries after close",
              b->exits == (current == b ? 1 : 0) && b->cleanups == (current == b ? 1 : 0) &&
              a->exits == 1 && a->cleanups == 1 && countsMatch(win, 0) &&
              win.context().appEventListeners.empty() && internal::attachedApps().empty(), nth);
        if (sg_isvalid()) internal::drainPendingGpuDestroys();
        if (!injected) {
            std::printf("swap boundary=%d: %d fired indices, terminal index passed\n", callbackBoundary, nth);
            break;
        }
    }
}

extern "C" void attachAtCallbackBoundary(Window* win) noexcept {
    internal::windowCallback([&] { internal::applyPendingAppForTests(*win); });
}
bool attachment(int scenario, bool callback, int nth) {
    AttachmentDevice device;
    TestWindow win;
    auto a = std::make_shared<TestApp>(), b = std::make_shared<TestApp>();
    auto superseded = std::make_shared<TestApp>();
    const bool hasOld = scenario != 1;
    auto& audio = AudioEngine::getInstance();
    const auto audioOutBefore = audio.audioOut.listenerCount();
    const auto audioInBefore = audio.audioIn.listenerCount();
    if (hasOld) {
        win.setApp(a); internal::applyPendingAppForTests(win);
        internal::attachAppAudio(*a);
    }
    prepareReleasedTarget(win);
    if (scenario == 3) win.setApp(superseded);
    win.setApp(scenario == 2 ? nullptr : b);
    if (scenario == 4) win.setApp(a); // Cancel a pending swap with the current App.
    auto old = win.getApp();
    auto requested = scenario == 2 ? std::shared_ptr<TestApp>{} : (scenario == 4 ? a : b);
    auto* previous = internal::currentWindowCtx();
    const auto gpuContext = sg_isvalid() ? sgl_get_context() : sgl_context{};
    bool escaped = false;
    arm(nth);
    try {
        if (callback) attachAtCallbackBoundary(&win);
        else internal::applyPendingAppForTests(win);
    } catch (...) { escaped = true; }
    const bool injected = fired;
    disarm();
    const auto current = win.getApp();
    const bool committed = current == requested;
    check("attachment: atomic App/root/registry/bindings, request dropped, context restored",
          !escaped && (committed || current == old) &&
          win.context().rootNode.lock() == current &&
          countsMatch(win, current ? 1 : 0) &&
          win.context().appEventListeners.size() == (current ? 15u : 0u) &&
          internal::attachedApps().size() == (current ? 1u : 0u) &&
          internal::attachedApps().count(a.get()) == (current == a ? 1u : 0u) &&
          internal::attachedApps().count(b.get()) == (current == b ? 1u : 0u) &&
          internal::attachedApps().count(superseded.get()) == 0 &&
          !win.appRequested_ && !win.pendingApp_ &&
          a->exits == (hasOld && current != a ? 1 : 0) &&
          a->cleanups == (hasOld && current != a ? 1 : 0) &&
          internal::appRanCleanup(*a) == (hasOld && current != a) &&
          audio.audioOut.listenerCount() == audioOutBefore + (current == a ? 1u : 0u) &&
          audio.audioIn.listenerCount() == audioInBefore + (current == a ? 1u : 0u) &&
          !internal::appRanCleanup(*b) && b->exits == 0 && b->cleanups == 0 &&
          superseded->exits == 0 && superseded->cleanups == 0 &&
          internal::currentWindowCtx() == previous &&
          (!sg_isvalid() || sgl_get_context().id == gpuContext.id), nth);
    // A failed or superseded candidate must have no delivery bindings. An
    // update exercises every live update token, including provisional ones.
    internal::currentWindowCtx() = &win.context();
    win.events().update.notify();
    internal::restoreWindowContext(previous);
    check("attachment: only attached App receives delivery",
          a->updates == (current == a ? 1 : 0) && b->updates == (current == b ? 1 : 0) &&
          superseded->updates == 0, nth);
    if (current != b) {
        TestWindow retry;
        retry.setApp(b); internal::applyPendingAppForTests(retry);
        check("attachment: unattached B is accepted by another window",
              retry.getApp() == b && countsMatch(retry, 1) &&
              retry.context().appEventListeners.size() == 15, nth);
        retry.finish();
    }
    win.finish();
    check("attachment: no stale registry or bindings after close",
          internal::attachedApps().empty() && countsMatch(win, 0) &&
          win.context().appEventListeners.empty() && internal::currentWindowCtx() == previous, nth);
    if (sg_isvalid()) internal::drainPendingGpuDestroys();
    return injected;
}
void attachments() {
    for (bool callback : {false, true}) for (int scenario = 0; scenario < 5; ++scenario) {
        int nth = 0;
        while (attachment(scenario, callback, nth)) ++nth;
        std::printf("attachment sweep scenario=%d callback=%d gpu=%d: %d fired indices, terminal index passed\n",
                    scenario, callback, AttachmentDevice::usesGpu, nth);
    }
    struct RequestOnExit : TestApp {
        Window& win;
        std::shared_ptr<App> next;
        RequestOnExit(Window& w, std::shared_ptr<App> n) : win(w), next(std::move(n)) {}
        void exit() override { TestApp::exit(); win.setApp(next); }
    };
    AttachmentDevice device;
    TestWindow win;
    auto b = std::make_shared<TestApp>(), c = std::make_shared<TestApp>();
    auto a = std::make_shared<RequestOnExit>(win, c);
    win.setApp(a); internal::applyPendingAppForTests(win);
    win.setApp(b); internal::applyPendingAppForTests(win);
    check("attachment: outgoing exit can queue the following swap",
          win.getApp() == b && win.pendingApp_ == c && win.appRequested_);
    internal::applyPendingAppForTests(win);
    check("attachment: following request publishes only at its own boundary",
          win.getApp() == c && !win.appRequested_ && !win.pendingApp_ &&
          a->exits == 1 && b->exits == 1 && countsMatch(win, 1));
}

// Reclaim a tombstoned capture while the replacement's update snapshot is
// prepared. This is user code inside framework preparation, before commit.
struct ReenterOnRelease {
    std::function<void()> work;
    int& releases;
    ReenterOnRelease(std::function<void()> f, int& r)
        : work(std::move(f)), releases(r) {}
    ~ReenterOnRelease() noexcept {
        ++releases;
        internal::windowCallback(work);
    }
};
template<class T>
void retainUntilRebuild(Event<T>& event, std::function<void()> work, int& releases) {
    auto capture = std::make_shared<ReenterOnRelease>(std::move(work), releases);
    auto victim = event.listen([capture](auto&...) {});
    capture.reset();
    arm(0); victim.disconnect(); disarm();
    check("reentry fixture: failed disconnect retains capture", fired && releases == 0);
}
// Guest App destruction must retire deferred producers under every allocation
// failure, without invoking them or reordering another owner's replies.
void mcpOwnerRetirement() {
    namespace mi = mcp::internal;
    mi::Server::instance();
    mi::ownerCleanupHooks();
    mi::deferredResponsesMutex(); // Keep cold initialization from shifting sweep indices.
    for (int nth = 0;; ++nth) {
        int ownerTag = 0, drops = 0, produced = 0;
        const void* owner = &ownerTag;
        mi::setRegistrationOwner(owner);
        mcp::tool("window_owner_fault_probe", "owned retirement probe").bind([] { return 1; });
        mcp::status("window_owner_fault_status", [] { return 1; });
        mcp::statusImage("window_owner_fault_image", [] { return Pixels(); });
        mi::setRegistrationOwner(nullptr);
        struct Capture {
            int& drops;
            explicit Capture(int& count) : drops(count) {}
            ~Capture() { ++drops; }
        };
        std::vector<std::future<mi::ReplyThunk>> replies;
        std::vector<std::shared_ptr<std::promise<mi::ReplyThunk>>> promises;
        for (int i = 0; i < 4; ++i) {
            mi::DeferredResponse item;
            item.id = i;
            item.owner = i % 2 ? owner : nullptr;
            item.budget = std::make_shared<mi::WorkBudget>();
            item.response = std::make_shared<std::promise<mi::ReplyThunk>>();
            replies.push_back(item.response->get_future());
            promises.push_back(item.response);
            auto capture = i % 2 ? std::make_shared<Capture>(drops) : nullptr;
            item.makeEnvelope = [capture, &produced]() -> mi::ReplyThunk {
                ++produced;
                return [] { return std::string("{}"); };
            };
            mi::deferredResponses().push_back(std::move(item));
        }
        arm(nth);
        mi::removeRegistrationsOwnedBy(owner);
        const bool injected = fired;
        disarm();
        auto& pending = mi::deferredResponses();
        check("MCP owner: all producers retired, host FIFO retained", drops == 2 && produced == 0 &&
              pending.size() == 2 && pending[0].id == 0 && pending[1].id == 2, nth);
        check("MCP owner: tool removed", !mcp::hasTool("window_owner_fault_probe"), nth);
        check("MCP owner: replies terminal before return",
              replies[1].wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
              replies[3].wait_for(std::chrono::seconds(0)) == std::future_status::ready, nth);
        pending.clear();
        if (!injected) {
            std::printf("MCP owner retirement: %d fired indices, terminal index passed\n", nth);
            break;
        }
    }
}
void mcpOwnerReentry() {
    namespace mi = mcp::internal;
    int ownerTag = 0, ownedRuns = 0, hostRuns = 0;
    const void* owner = &ownerTag;
    for (int i = 0; i < 3; ++i) {
        mi::DeferredResponse item;
        item.id = i;
        item.owner = i == 1 ? nullptr : owner;
        item.budget = std::make_shared<mi::WorkBudget>();
        item.response = std::make_shared<std::promise<mi::ReplyThunk>>();
        item.makeEnvelope = [&, owned = i != 1]() -> mi::ReplyThunk {
            if (owned) ++ownedRuns; else ++hostRuns;
            return [] { return std::string("{}"); };
        };
        mi::deferredResponses().push_back(std::move(item));
    }
    const auto hooksBefore = mi::ownerCleanupHooks().size();
    mi::ownerCleanupHooks().push_back([&](const void* retiring) {
        if (retiring == owner) mi::drainDeferredResponses();
    });
    mi::removeRegistrationsOwnedBy(owner);
    mi::ownerCleanupHooks().resize(hooksBefore);
    check("MCP owner: reentrant drain never invokes retired producers",
          ownedRuns == 0 && hostRuns == 1 && mi::deferredResponses().empty());
}
void mainBoundaryValidation() {
    internal::createMainWindow(WindowSettings{});
    auto* win = internal::mainWindow();
    internal::WindowRequestAccess::openMain(*win);
    auto current = std::make_shared<TestApp>(), candidate = std::make_shared<TestApp>();
    check("main boundary: initial shared attach", internal::attachMainApp(current));
    int releases = 0;
    retainUntilRebuild(win->events().update, [] {}, releases);
    win->setApp(candidate);
    TestWindow other;
    other.setApp(candidate); attachAtCallbackBoundary(&other); other.finish();
    internal::applyPendingAppForTests(*win);
    check("main boundary: invalid candidate rejected before preparation", win->getApp() == current && releases == 0);
    internal::destroyMainWindow();
}
// The same preparation-recheck invariant with the runner-owned main Window.
void mainReentrantAttachments() {
    for (int action = 0; action < 2; ++action) {
        internal::createMainWindow(WindowSettings{});
        auto* win = internal::mainWindow();
        internal::WindowRequestAccess::openMain(*win);
        auto current = std::make_shared<TestApp>(), candidate = std::make_shared<TestApp>();
        check("main recheck: initial shared attach", internal::attachMainApp(current));
        TestWindow other;
        int releases = 0;
        retainUntilRebuild(win->events().update, [&] {
            other.setApp(candidate);
            attachAtCallbackBoundary(&other);
            check("main recheck: competitor attached during preparation", other.getApp() == candidate);
            if (action == 1) other.finish();
        }, releases);
        win->setApp(candidate);
        internal::applyPendingAppForTests(*win);
        check("main recheck: candidate rejected after preparation", releases == 1 && win->getApp() == current &&
              (action == 0 ? other.getApp() == candidate : internal::appRanCleanup(*candidate)));
        other.finish();
        internal::destroyMainWindow();
        check("main recheck: exactly once teardown", current->exits == 1 && current->cleanups == 1 &&
              candidate->exits == 1 && candidate->cleanups == 1 && internal::attachedApps().empty());
    }
}
void reentrantAttachments() {
    for (bool callback : {false, true}) for (int action = 0; action < 5; ++action) {
        for (int nth = 0;; ++nth) {
            AttachmentDevice device;
            TestWindow win, other;
            auto a = std::make_shared<TestApp>(), b = std::make_shared<TestApp>();
            auto c = std::make_shared<TestApp>();
            win.setApp(a); internal::applyPendingAppForTests(win);
            int releases = 0;
            retainUntilRebuild(win.events().update, [&] {
                check("preparation reentry: existing committed App/root/registry",
                      win.getApp() == a && win.context().rootNode.lock() == a &&
                      internal::attachedApps().size() == 1 && internal::attachedApps().count(a.get()));
                win.events().update.notify();
                check("preparation reentry: A updates exactly once, B stays inert",
                      a->updates == 1 && b->updates == 0);
                if (action == 1 || action == 2) {
                    other.setApp(b); attachAtCallbackBoundary(&other);
                    check("preparation reentry: nested window owns B", other.getApp() == b);
                    if (action == 2) other.finish(); // B is now unavailable by lifecycle.
                } else if (action == 3) {
                    win.setApp(c); attachAtCallbackBoundary(&win);
                    check("preparation reentry: same-window request waits", win.getApp() == a && win.pendingApp_ == c);
                } else if (action == 4) win.finish();
                // Exhaust every remaining allocation after user reentry. The
                // existing attachment matrices cover faults before reclamation.
                arm(nth);
            }, releases);
            win.setApp(b);
            if (callback) attachAtCallbackBoundary(&win);
            else internal::applyPendingAppForTests(win);
            const bool injected = fired;
            disarm();
            const auto current = win.getApp();
            check("preparation reentry: one release, one A update", releases == 1 && a->updates == 1 && b->updates == 0, nth);
            check("preparation reentry: authoritative candidate validation",
                  (action == 1 || action == 2 ? current == a :
                   action == 4 ? !current : current == a || current == b) &&
                  win.context().rootNode.lock() == current && countsMatch(win, current ? 1 : 0) &&
                  internal::attachedApps().size() == (current ? 1u : 0u) + (other.getApp() ? 1u : 0u) &&
                  internal::attachedApps().count(a.get()) == (current == a ? 1u : 0u) &&
                  internal::attachedApps().count(b.get()) == (current == b || other.getApp() == b ? 1u : 0u), nth);
            if (action == 3) {
                check("preparation reentry: later request survives", win.appRequested_ && win.pendingApp_ == c, nth);
                attachAtCallbackBoundary(&win);
                check("preparation reentry: later boundary commits C", win.getApp() == c && countsMatch(win, 1), nth);
            }
            win.finish();
            check("preparation reentry: closing first preserves other registration",
                  internal::attachedApps().size() == (other.getApp() ? 1u : 0u) &&
                  (other.getApp() != b || (b->exits == 0 && b->cleanups == 0 && internal::attachedApps().count(b.get()))), nth);
            other.finish();
            const bool usedB = action == 1 || action == 2 || current == b;
            check("preparation reentry: exactly-once teardown, empty registry",
                  a->exits == 1 && a->cleanups == 1 && b->exits == (usedB ? 1 : 0) &&
                  b->cleanups == (usedB ? 1 : 0) && internal::attachedApps().empty(), nth);
            if (sg_isvalid()) internal::drainPendingGpuDestroys();
            if (!injected) {
                std::printf("reentrant attachment action=%d callback=%d gpu=%d: %d fired indices, terminal index passed\n",
                            action, callback, AttachmentDevice::usesGpu, nth);
                break;
            }
        }
    }
}

void reentrantOtherPhases() {
    for (bool callback : {false, true}) {
        AttachmentDevice device;
        TestWindow win, other;
        auto a = std::make_shared<TestApp>(), b = std::make_shared<TestApp>(), c = std::make_shared<TestApp>();
        win.setApp(a); internal::applyPendingAppForTests(win);
        int preparationReleases = 0, retirementReleases = 0;
        // Last preparation step leaves a fresh tombstone in an already
        // prepared event. Disconnecting the old bindings reclaims it at commit.
        retainUntilRebuild(win.events().filesDropped, [&] {
            retainUntilRebuild(win.events().update, [&] {
                check("retirement reentry: new App/root/registry committed",
                      win.getApp() == b && win.context().rootNode.lock() == b &&
                      internal::attachedApps().size() == 1 && internal::attachedApps().count(b.get()));
                win.events().update.notify();
                check("retirement reentry: B updates once, retired A stays inert", b->updates == 1 && a->updates == 0);
                other.setApp(a); attachAtCallbackBoundary(&other);
                check("retirement reentry: outgoing A cannot attach elsewhere", !other.getApp() && !other.appRequested_);
                win.setApp(c); attachAtCallbackBoundary(&win);
                check("retirement reentry: later request stays pending", win.getApp() == b && win.pendingApp_ == c);
            }, retirementReleases);
        }, preparationReleases);
        win.setApp(b);
        if (callback) attachAtCallbackBoundary(&win);
        else internal::applyPendingAppForTests(win);
        check("retirement reentry: both captures released once", preparationReleases == 1 && retirementReleases == 1);
        attachAtCallbackBoundary(&win);
        check("retirement reentry: subsequent request commits", win.getApp() == c && a->exits == 1 && a->cleanups == 1 && b->exits == 1 && b->cleanups == 1);
        win.finish(); other.finish();
        check("retirement reentry: registry drained", internal::attachedApps().empty());
    }
    for (bool closing : {false, true}) {
        struct ExitReentry : TestApp {
            TestWindow& other;
            explicit ExitReentry(TestWindow& w) : other(w) {}
            void exit() override {
                TestApp::exit();
                other.setApp(std::static_pointer_cast<App>(shared_from_this()));
                attachAtCallbackBoundary(&other);
                check("exit reentry: outgoing App cannot attach elsewhere", !other.getApp() && !other.appRequested_);
            }
        };
        TestWindow win, other;
        auto a = std::make_shared<ExitReentry>(other);
        win.setApp(a); internal::applyPendingAppForTests(win);
        if (closing) win.finish();
        else { win.setApp(nullptr); attachAtCallbackBoundary(&win); }
        check("exit reentry: one exit and cleanup", a->exits == 1 && a->cleanups == 1 && internal::attachedApps().empty());
    }
    {
        TestWindow win;
        auto a = std::make_shared<TestApp>(), b = std::make_shared<TestApp>(), c = std::make_shared<TestApp>();
        struct PendingReentry : TestApp {
            TestWindow& win;
            std::shared_ptr<App> next;
            PendingReentry(TestWindow& w, std::shared_ptr<App> n) : win(w), next(std::move(n)) {}
            ~PendingReentry() noexcept override {
                internal::windowCallback([&] { win.setApp(next); attachAtCallbackBoundary(&win); });
            }
        };
        win.setApp(a); internal::applyPendingAppForTests(win);
        win.setApp(std::make_shared<PendingReentry>(win, c));
        win.setApp(b); // Releases the previous request, which requests C.
        attachAtCallbackBoundary(&win);
        check("pending release reentry: later request wins", win.getApp() == c && b->exits == 0 && b->cleanups == 0);
    }
    {
        TestWindow win;
        auto a = std::make_shared<TestApp>(), b = std::make_shared<TestApp>(), c = std::make_shared<TestApp>();
        win.setApp(a); internal::applyPendingAppForTests(win);
        int reports = 0;
        auto logger = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level != LogLevel::Error || reports++) return;
            check("logger reentry: failed preparation retains A", win.getApp() == a && win.context().rootNode.lock() == a);
            win.events().update.notify();
            win.setApp(c); attachAtCallbackBoundary(&win);
            check("logger reentry: nested boundary retains A", win.getApp() == a && win.pendingApp_ == c);
        });
        win.setApp(b); arm(0); attachAtCallbackBoundary(&win); disarm();
        check("logger reentry: new request survives failure report", reports == 1 && a->updates == 1 && win.pendingApp_ == c);
        attachAtCallbackBoundary(&win);
        check("logger reentry: subsequent boundary commits C", win.getApp() == c);
    }
}

struct ClearExitApp : TestApp {
    TestWindow& win;
    bool faultInsideExit;
    int clearAllocations = -1;
    bool finished = false;
    ClearExitApp(TestWindow& w, bool fault) : win(w), faultInsideExit(fault) {}
    void exit() override {
        ++exits;
        if (faultInsideExit) arm(0); // The review's one-shot fault at exit.clear().
        const int before = attempts;
        win.events().exit.clear();
        clearAllocations = attempts - before;
        // Leave injection armed through binding removal and audio detachment.
    }
    void cleanup() override {
        ++cleanups;
        finished = win.context().appEventListeners.empty() && internal::appRanCleanup(*this);
    }
};
extern "C" void closeAtCallbackBoundary(TestWindow* win) noexcept { win->finish(); }

int closeWithClear(int boundary, int nth, bool faultInsideExit = false) {
    auto win = std::make_shared<TestWindow>();
    auto app = std::make_shared<ClearExitApp>(*win, faultInsideExit);
    win->setApp(app); internal::applyPendingAppForTests(*win);
    int after = 0, ordinary = 0;
    auto mandatory = internal::FrameworkEventAccess::listen(win->events().exit, [&] {
        ++after;
        // Protected teardown still runs after clear and before cleanup.
        if (app->exits != 1 || app->cleanups != 0) ++failures;
    }, EventPriority::AfterApp);
    auto user = win->events().exit.listen([&] { ++ordinary; }, EventPriority::AfterApp);
    auto* previous = internal::currentWindowCtx();
    bool escaped = false;
    arm(nth);
    try {
        if (boundary == 0) win->finish();
        else if (boundary == 1) closeAtCallbackBoundary(win.get());
        else win.reset(); // shared_ptr destruction and ~Window are noexcept.
    } catch (const std::bad_alloc&) { escaped = true; }
    const int total = attempts;
    const bool injected = fired;
    disarm();
    check("exit.clear: allocation-free, mandatory teardown and restored context",
          !escaped && app->clearAllocations == 0 && app->exits == 1 && app->cleanups == 1 &&
          app->finished && after == 1 && ordinary == 0 && internal::currentWindowCtx() == previous &&
          internal::attachedApps().empty() && (nth < 0 || injected), nth);
    mandatory.disconnect();
    if (win) {
        win->finish();
        check("exit.clear: repeated close leaves all bindings disconnected",
              countsMatch(*win, 0) && win->context().appEventListeners.empty() &&
              app->exits == 1 && app->cleanups == 1, nth);
    }
    return total;
}
void closes() {
    // Every measured allocation through clear-from-exit, including disconnect
    // snapshot/proxy allocations on MSVC Debug. Also test the exact exit arm
    // independently of allocations earlier in native teardown.
    for (int boundary = 0; boundary < 3; ++boundary) {
        closeWithClear(boundary, -1); // Warm process-wide teardown state.
        const int total = closeWithClear(boundary, -1);
        std::printf("exit.clear boundary %d: %d measured allocations\n", boundary, total);
        check("exit.clear: fault hook has measurable teardown allocations", total > 0);
        for (int nth = 0; nth < total; ++nth) closeWithClear(boundary, nth);
        closeWithClear(boundary, 0, true);
    }
}

// Based on the review's queued_close.cpp. Sweep the entire sequence until a
// one-shot fault does not fire, including allocations in failure reporting and
// paths reached only after an earlier failure. No STL allocation sizes/counts.
struct CloseState {
    int exits = 0, cleanups = 0, after = 0, destroyed = 0;
    bool detached = false, bindingsEmpty = false, screenshotsEmpty = false;
    bool destructorContext = false;
};
void allocatingWork() {
    // Explicit call cannot be elided as a new/delete expression can be.
    void* memory = ::operator new(8192);
    ::operator delete(memory);
}
struct SweepApp : App {
    TestWindow& win;
    CloseState& state;
    bool clearExit, throwExit, throwCleanup;
    SweepApp(TestWindow& w, CloseState& s, bool clear, int throwing)
        : win(w), state(s), clearExit(clear), throwExit(throwing == 1), throwCleanup(throwing == 2) {}
    void exit() override {
        ++state.exits;
        if (clearExit) win.events().exit.clear();
        allocatingWork();
        if (throwExit) throw 1;
    }
    void cleanup() override {
        ++state.cleanups;
        state.detached = internal::appRanCleanup(*this) && !win.getApp() &&
                         win.context().rootNode.expired() && internal::attachedApps().empty();
        state.bindingsEmpty = win.context().appEventListeners.empty();
        state.screenshotsEmpty = win.context().asyncScreenshots.pending.empty();
        allocatingWork();
        if (throwCleanup) throw 2;
    }
    ~SweepApp() override {
        ++state.destroyed;
        state.destructorContext = internal::currentWindowCtx() == &win.context();
    }
};
extern "C" void sweepCloseCallback(TestWindow* win) noexcept {
    internal::windowCallback([&] { win->finish(); });
}
bool sweepClose(int boundary, int queued, bool clearExit, int throwing, int nth) {
    CloseState state;
    auto win = std::make_unique<TestWindow>();
    auto app = std::make_shared<SweepApp>(*win, state, clearExit, throwing);
    std::weak_ptr<App> lifetime = app;
    win->setApp(app); internal::applyPendingAppForTests(*win);
    app.reset(); // Teardown owns the last App reference; completions must not retain it.
    auto first = internal::FrameworkEventAccess::listen(win->events().exit, [&] {
        ++state.after;
        allocatingWork();
        if (throwing == 3) throw 3;
    }, EventPriority::AfterApp);
    auto last = internal::FrameworkEventAccess::listen(win->events().exit, [&] { ++state.after; }, EventPriority::AfterApp);
    auto before = win->events().exit.listen([&] { if (throwing == 4) throw 4; }, EventPriority::BeforeApp);
    for (int i = 0; i < queued; ++i)
        internal::submitScreenshot(&win->context().asyncScreenshots, "pending-close-sweep.png");
    auto admitted = win->context().asyncScreenshots.admitted;
    // Exercise the allocating dropped-request diagnostic before exit as well.
    win->setApp(std::make_shared<App>());
    int completions = 0;
    bool validCompletions = true;
    auto completed = onScreenshotSave().listen([&](ScreenshotSaveEventArgs& args) {
        ++completions;
        validCompletions &= args.result.error == SaveError::WriteFailed && !args.path.empty() && lifetime.expired();
    });
    auto* previous = internal::currentWindowCtx();
    arm(nth);
    bool escaped = false;
    try {
        if (boundary == 0) win->finish();
        else if (boundary == 1) sweepCloseCallback(win.get());
        else win.reset();
    } catch (...) { escaped = true; }
    const bool injected = fired;
    disarm();
    check("close sweep: exit, all protected hooks, detach, cleanup, lifetime and context",
          !escaped && state.exits == 1 && state.cleanups == 1 && state.after == 2 &&
          state.destroyed == 1 && state.detached && state.bindingsEmpty && state.screenshotsEmpty &&
          state.destructorContext && lifetime.expired() && internal::currentWindowCtx() == previous, nth);
    if (win) {
        win->finish();
        check("close sweep: retry inert, queue drained, bindings disconnected",
              win->context().asyncScreenshots.pending.empty() && win->context().appEventListeners.empty() &&
              state.exits == 1 && state.cleanups == 1 && state.after == 2, nth);
    }
    // Accepted completions own only ScreenshotState. Destroying/delivering the
    // batch releases every slot; failed cancellation dropped its slot already.
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    check("close sweep: failure completion or drop, no leaked admission or retained App",
          *admitted == 0 && lifetime.expired() && validCompletions &&
          completions <= queued && completions >= queued - (injected ? 1 : 0), nth);
    return injected;
}
void closeSweep() {
    auto savedRun = internal::appRunToken();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    // Warm lazy framework state before the repeatable exhaustive matrices.
    sweepClose(0, 1, true, 0, -1);
    for (int boundary = 0; boundary < 3; ++boundary) {
        for (int queued : {0, 1, 3}) for (bool clear : {false, true}) {
            int nth = 0;
            while (sweepClose(boundary, queued, clear, 0, nth)) ++nth;
            std::printf("close sweep boundary=%d queued=%d clear=%d: %d fired indices, terminal index passed\n",
                        boundary, queued, clear, nth);
        }
        // App exit/cleanup, an AfterApp hook, and a BeforeApp listener throwing
        // must each leave the remaining mandatory steps reachable.
        for (int throwing = 1; throwing <= 4; ++throwing) {
            int nth = 0;
            while (sweepClose(boundary, 3, true, throwing, nth)) ++nth;
            std::printf("close throwing sweep boundary=%d throwing=%d: %d fired indices\n", boundary, throwing, nth);
        }
    }
    // A queue can also die without a Window/endApp, including static cleanup.
    for (int queued : {1, 3}) {
        for (int nth = 0;; ++nth) {
            auto queue = std::make_unique<internal::ScreenshotQueue>();
            for (int i = 0; i < queued; ++i) internal::submitScreenshot(queue.get(), "pending-queue-destruction.png");
            auto admitted = queue->admitted;
            arm(nth);
            queue.reset(); // noexcept unique_ptr/queue destructor boundary
            const bool injected = fired;
            disarm();
            internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
            check("screenshot queue destructor: all requests completed or dropped", *admitted == 0, nth);
            if (!injected) break;
        }
    }
    internal::appRunToken() = std::move(savedRun);
}
void callbackFailure() {
    TestWindow win;
    auto* previous = internal::currentWindowCtx();
    internal::windowCallback([&] {
        internal::currentWindowCtx() = &win.context();
        internal::WindowDispatchScope dispatch(win);
        throw 1;
    });
    check("native callback: thrown dispatch restores context and depth",
          internal::currentWindowCtx() == previous && win.dispatchDepth_ == 0);
}
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    getMainThreadId();
#ifdef TC_WINDOW_ATTACH_DUMMY
    swaps(); swaps(true); attachments(); reentrantAttachments(); reentrantOtherPhases();
#else
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "all" || mode == "owner") { mcpOwnerRetirement(); mcpOwnerReentry(); }
    if (mode == "all" || mode == "main") { mainBoundaryValidation(); mainReentrantAttachments(); }
    if (mode == "all" || mode == "typed") { removal<int>(); registration<int>(); }
    if (mode == "all" || mode == "void") { removal<void>(); registration<void>(); }
    if (mode == "all" || mode == "swap") swaps();
    if (mode == "all" || mode == "callback") swaps(true);
    if (mode == "all" || mode == "attach") attachments();
    if (mode == "all" || mode == "reentrant") { reentrantAttachments(); reentrantOtherPhases(); }
    if (mode == "all" || mode == "close") closes();
    if (mode == "all" || mode == "sweep") { closeSweep(); callbackFailure(); }
#endif
    std::printf("allocation regressions: %d failures\n", failures);
    return failures ? 1 : 0;
}
