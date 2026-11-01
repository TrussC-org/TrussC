#include "../../common/tcWindowTest.h"
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
    Window renderWindow;
    internal::WindowContext* previous = internal::currentWindowCtx();
    AttachmentDevice() {
        sg_desc sd{}; sd.pipeline_pool_size = 2048;
        sd.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        sd.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        sd.environment.defaults.sample_count = 1;
        sg_setup(&sd);
        check("attachment dummy device initialized", sg_isvalid());
        sgl_desc_t gd{}; gd.context_pool_size = 8; gd.pipeline_pool_size = 256;
        sgl_setup(&gd);
        internal::WindowRequestAccess::bindForTests(renderWindow, 303);
        auto& main = trussc::internal::WindowRequestAccess::context(renderWindow);
        main.fbWidth = main.fbHeight = 32;
        main.swapchainTarget.width = main.swapchainTarget.height = 32;
        internal::restoreWindowContext(&main);
    }
    ~AttachmentDevice() {
        internal::WindowRequestAccess::teardown(renderWindow);
        internal::restoreWindowContext(previous);
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
    if (!tcTest::skipProxyAllocationSweep("event removal snapshot sweep"))
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
    if (tcTest::skipProxyAllocationSweep("event registration snapshot sweep")) return;
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
    TestWindow() { native_ = &nativeStandIn; internal::WindowRequestAccess::bindForTests(*this, 202); }
    void finish() noexcept {
        if (!native_) return;
        // The synthetic handle has no native backing; use the production ending
        // and binding retirement rather than disposing the target before App.
        native_ = nullptr;
        internal::WindowRequestAccess::teardown(*this);
    }
    ~TestWindow() { finish(); }
};
struct TestApp : App {
    int updates = 0, exits = 0, cleanups = 0;
    void update() override { ++updates; }
    void exit() override { ++exits; }
    void cleanup() override { ++cleanups; }
};
bool countsMatch(Window& win, std::size_t) {
    // Stable framework slots belong to the open Window, including when empty.
    const std::size_t count = win.isOpen() ? 1 : 0;
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
    auto& ctx = trussc::internal::WindowRequestAccess::context(win);
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
    internal::WindowEntryScope scope(*win);
    internal::WindowRequestAccess::attachCandidate(*win, *next);
    arm(nth);
}
void swaps(bool callbackBoundary = false) {
    if (tcTest::skipProxyAllocationSweep("window App swap sweep")) return;
    int errors = 0;
    auto logger = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error) ++errors;
    });
    for (int nth = 0;; ++nth) {
        AttachmentDevice device;
        TestWindow win;
        auto a = internal::makeConstructedApp<TestApp>(), b = internal::makeConstructedApp<TestApp>();
        internal::WindowRequestAccess::attachCandidate(win, a); internal::applyPendingAppForTests(win);
        prepareReleasedTarget(win);
        const int before = errors;
        bool threw = false;
        if (callbackBoundary) {
            std::shared_ptr<App> next = b;
            swapAtCallbackBoundary(&win, &next, nth);
        } else {
            internal::WindowRequestAccess::attachCandidate(win, b);
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
              trussc::internal::WindowRequestAccess::context(win).appEventListeners.size() == 15u &&
              (errors - before <= 1 && (current == b || errors - before == 1)), nth);
        auto* prev = internal::currentWindowCtx();
        internal::currentWindowCtx() = &trussc::internal::WindowRequestAccess::context(win);
        win.events().update.notify();
        win.events().update.notify();
        internal::currentWindowCtx() = prev;
        check("swap: B updates once per frame after disconnect fault",
              b->updates == (current == b ? 2 : 0) && a->updates == (current == a ? 2 : 0), nth);
        win.finish(); win.finish();
        check("swap: B exit exactly once, no stale framework entries after close",
              b->exits == (current == b ? 1 : 0) && b->cleanups == (current == b ? 1 : 0) &&
              a->exits == 1 && a->cleanups == 1 && countsMatch(win, 0) &&
              trussc::internal::WindowRequestAccess::context(win).appEventListeners.empty() && internal::attachedApps().empty(), nth);
        if (sg_isvalid()) internal::drainPendingGpuDestroys();
        if (!injected) {
            std::printf("swap boundary=%d: %d fired indices, terminal index passed\n", callbackBoundary, nth);
            break;
        }
    }
}

extern "C" void attachAtCallbackBoundary(Window* win) noexcept {
    tcCoreTest::nativeBoundary([&] { internal::applyPendingAppForTests(*win); });
}
bool attachment(int scenario, bool callback, int nth) {
    AttachmentDevice device;
    TestWindow win;
    auto a = internal::makeConstructedApp<TestApp>(), b = internal::makeConstructedApp<TestApp>();
    auto superseded = internal::makeConstructedApp<TestApp>();
    const bool hasOld = scenario != 1;
    auto& audio = AudioEngine::getInstance();
    const auto audioOutBefore = audio.audioOut.listenerCount();
    const auto audioInBefore = audio.audioIn.listenerCount();
    if (hasOld) {
        internal::WindowRequestAccess::attachCandidate(win, a); internal::applyPendingAppForTests(win);
        internal::attachAppAudio(*a);
    }
    prepareReleasedTarget(win);
    if (scenario == 3) internal::WindowRequestAccess::attachCandidate(win, superseded);
    internal::WindowRequestAccess::attachCandidate(win, scenario == 2 ? nullptr : b);
    if (scenario == 4) internal::WindowRequestAccess::attachCandidate(win, superseded); // Last fresh candidate wins.
    auto old = win.getApp();
    auto requested = scenario == 2 ? std::shared_ptr<TestApp>{} : (scenario == 4 ? superseded : b);
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
          trussc::internal::WindowRequestAccess::context(win).rootNode.lock() == current &&
          countsMatch(win, current ? 1 : 0) &&
          trussc::internal::WindowRequestAccess::context(win).appEventListeners.size() == 15u &&
          internal::attachedApps().size() == (current ? 1u : 0u) &&
          internal::attachedApps().count(a.get()) == (current == a ? 1u : 0u) &&
          internal::attachedApps().count(b.get()) == (current == b ? 1u : 0u) &&
          internal::attachedApps().count(superseded.get()) == (current == superseded ? 1u : 0u) &&
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
    internal::currentWindowCtx() = &trussc::internal::WindowRequestAccess::context(win);
    win.events().update.notify();
    internal::restoreWindowContext(previous);
    check("attachment: only attached App receives delivery",
          a->updates == (current == a ? 1 : 0) && b->updates == (current == b ? 1 : 0) &&
          superseded->updates == (current == superseded ? 1 : 0), nth);
    if (current != b && !internal::appOwner(*b)->token->load()) {
        TestWindow retry;
        internal::WindowRequestAccess::attachCandidate(retry, b); internal::applyPendingAppForTests(retry);
        check("attachment: a cancelled candidate cannot attach elsewhere",
              !retry.getApp() && !internal::appOwner(*b)->token->load() && countsMatch(retry, 0) &&
              trussc::internal::WindowRequestAccess::context(retry).appEventListeners.size() == 15, nth);
        retry.finish();
    }
    win.finish();
    check("attachment: no stale registry or bindings after close",
          internal::attachedApps().empty() && countsMatch(win, 0) &&
          trussc::internal::WindowRequestAccess::context(win).appEventListeners.empty() && internal::currentWindowCtx() == previous, nth);
    if (sg_isvalid()) internal::drainPendingGpuDestroys();
    return injected;
}
void attachments() {
    if (!tcTest::skipProxyAllocationSweep("window App attachment sweep"))
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
        void exit() override { TestApp::exit(); internal::WindowRequestAccess::attachCandidate(win, next); }
    };
    AttachmentDevice device;
    TestWindow win;
    auto b = internal::makeConstructedApp<TestApp>(), c = internal::makeConstructedApp<TestApp>();
    auto a = internal::makeConstructedApp<RequestOnExit>(win, c);
    internal::WindowRequestAccess::attachCandidate(win, a); internal::applyPendingAppForTests(win);
    internal::WindowRequestAccess::attachCandidate(win, b); internal::applyPendingAppForTests(win);
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
        tcCoreTest::nativeBoundary(work);
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
// Stable slots never rebuild on swap. Tombstones can survive a swap after an
// OOM disconnect; cancellation still releases App captures at owner retirement.
void mainBoundaryValidation() {
    TestWindow win;
    win.setApp<TestApp>(); attachAtCallbackBoundary(&win);
    auto old = std::static_pointer_cast<TestApp>(win.getApp());
    auto slots = trussc::internal::WindowRequestAccess::context(win).appEventListeners.size();
    int releases = 0;
    retainUntilRebuild(win.events().update, [] {}, releases);
    win.setApp<TestApp>(); attachAtCallbackBoundary(&win);
    check("stable slots: swap does not reclaim or reorder unrelated listeners",
          win.getApp() != old && releases == 0 && old->exits == 1 && old->cleanups == 1 &&
          trussc::internal::WindowRequestAccess::context(win).appEventListeners.size() == slots);
    auto rebuild = win.events().update.listen([] {});
    check("stable slots: explicit rebuild releases the tombstone once", releases == 1);
}
void mainReentrantAttachments() {
    TestWindow win;
    struct Nested : TestApp {
        Window& target;
        explicit Nested(Window& w) : target(w) {
            target.setApp<TestApp>();
            throw std::runtime_error("constructor rollback");
        }
    };
    win.setApp<TestApp>(); attachAtCallbackBoundary(&win);
    auto old = std::static_pointer_cast<TestApp>(win.getApp());
    bool threw = false;
    try { win.setApp<Nested>(win); } catch (const std::runtime_error&) { threw = true; }
    check("constructor reentry: throwing outer leaves old published and nested request pending",
          threw && win.getApp() == old && win.appRequested_);
    attachAtCallbackBoundary(&win);
    check("constructor reentry: nested candidate publishes once", win.getApp() != old && old->exits == 1);
}
void reentrantAttachments() {
    for (bool callback : {false, true}) for (bool closing : {false, true}) {
        AttachmentDevice device;
        TestWindow win, peer;
        struct Reentrant : TestApp {
            Window& win; Window& peer; bool closing;
            Reentrant(Window& w, Window& p, bool c) : win(w), peer(p), closing(c) {}
            void exit() override {
                TestApp::exit();
                peer.setApp<TestApp>();
                internal::applyPendingAppForTests(peer);
                if (closing) win.closeRequested_ = true;
                else win.setApp<TestApp>();
                internal::applyPendingAppForTests(win);
                check("exit reentry: outgoing App detached before nested work", !getWindow() && !win.getApp());
            }
        };
        win.setApp<Reentrant>(win, peer, closing); attachAtCallbackBoundary(&win);
        auto old = std::static_pointer_cast<TestApp>(win.getApp());
        win.setApp<TestApp>();
        if (callback) attachAtCallbackBoundary(&win); else internal::applyPendingAppForTests(win);
        check("exit reentry: peer survives and old ends once", peer.getApp() && old->exits == 1 && old->cleanups == 1);
        check("exit reentry: close outranks replacement, otherwise later request survives",
              closing ? !win.getApp() && !win.appRequested_ : win.getApp() && win.appRequested_);
        if (!closing) { auto current = win.getApp(); attachAtCallbackBoundary(&win);
            check("exit reentry: following boundary commits the later request", win.getApp() != current); }
        win.finish();
        check("exit reentry: closing one Window preserves peer attachment", peer.getApp() && internal::attachedApps().size() == 1);
        peer.finish();
        check("exit reentry: complete ledger retirement", internal::attachedApps().empty());
    }
}
void reentrantOtherPhases() {
    mainBoundaryValidation(); mainReentrantAttachments();
    TestWindow win;
    win.setApp<TestApp>(); attachAtCallbackBoundary(&win);
    auto old = win.getApp();
    int reports = 0;
    auto logger = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Error || reports++) return;
        check("logger reentry: failed ledger preparation retains old App", win.getApp() == old && trussc::internal::WindowRequestAccess::context(win).rootNode.lock() == old);
        win.setApp<TestApp>(); attachAtCallbackBoundary(&win);
    });
    win.setApp<TestApp>(); arm(0); attachAtCallbackBoundary(&win); disarm();
    check("logger reentry: later request survives failed preparation", reports == 1 && win.getApp() == old && win.appRequested_);
    attachAtCallbackBoundary(&win);
    check("logger reentry: next boundary commits the fresh candidate", win.getApp() != old);
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
        finished = trussc::internal::WindowRequestAccess::context(win).appEventListeners.size() == 15 && internal::appRanCleanup(*this);
    }
};
extern "C" void closeAtCallbackBoundary(TestWindow* win) noexcept { win->finish(); }

int closeWithClear(int boundary, int nth, bool faultInsideExit = false) {
    auto win = std::make_shared<TestWindow>();
    auto app = internal::makeConstructedApp<ClearExitApp>(*win, faultInsideExit);
    internal::WindowRequestAccess::attachCandidate(*win, app); internal::applyPendingAppForTests(*win);
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
              countsMatch(*win, 0) && trussc::internal::WindowRequestAccess::context(*win).appEventListeners.empty() &&
              app->exits == 1 && app->cleanups == 1, nth);
    }
    return total;
}
void closes() {
    if (tcTest::skipProxyAllocationSweep("window exit.clear teardown sweep")) return;
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

// Ordinary close and throwing callbacks keep the shared ending contract.
// Bookkeeping OOM has no guaranteed completion contract (owner, 10/31).
struct CloseState {
    int exits = 0, cleanups = 0, after = 0, destroyed = 0;
    bool detached = false, slotsStable = false;
    bool destructorContext = false;
};
struct CloseSmokeApp : App {
    TestWindow& win;
    CloseState& state;
    bool clearExit, throwExit, throwCleanup;
    CloseSmokeApp(TestWindow& w, CloseState& s, bool clear, int throwing)
        : win(w), state(s), clearExit(clear), throwExit(throwing == 1), throwCleanup(throwing == 2) {}
    void exit() override {
        ++state.exits;
        if (clearExit) win.events().exit.clear();
        if (throwExit) throw 1;
    }
    void cleanup() override {
        ++state.cleanups;
        state.detached = internal::appRanCleanup(*this) && !win.getApp() &&
                         trussc::internal::WindowRequestAccess::context(win).rootNode.expired() && internal::attachedApps().empty();
        state.slotsStable = trussc::internal::WindowRequestAccess::context(win).appEventListeners.size() == 15;
        if (throwCleanup) throw 2;
    }
    ~CloseSmokeApp() override {
        ++state.destroyed;
        state.destructorContext = internal::currentWindowCtx() == &trussc::internal::WindowRequestAccess::context(win);
    }
};
extern "C" void smokeCloseCallback(TestWindow* win) noexcept {
    tcCoreTest::nativeBoundary([&] { win->finish(); });
}
void smokeClose(int boundary, bool clearExit, int throwing) {
    CloseState state;
    auto win = std::make_unique<TestWindow>();
    auto app = internal::makeConstructedApp<CloseSmokeApp>(*win, state, clearExit, throwing);
    std::weak_ptr<App> lifetime = app;
    internal::WindowRequestAccess::attachCandidate(*win, app); internal::applyPendingAppForTests(*win);
    app.reset(); // Teardown owns the last App reference; completions must not retain it.
    auto first = internal::FrameworkEventAccess::listen(win->events().exit, [&] {
        ++state.after;
        if (throwing == 3) throw 3;
    }, EventPriority::AfterApp);
    auto last = internal::FrameworkEventAccess::listen(win->events().exit, [&] { ++state.after; }, EventPriority::AfterApp);
    auto before = win->events().exit.listen([&] { if (throwing == 4) throw 4; }, EventPriority::BeforeApp);
    // Exercise the allocating dropped-request diagnostic before exit as well.
    win->setApp<App>();
    auto* previous = internal::currentWindowCtx();
    bool escaped = false;
    try {
        if (boundary == 0) win->finish();
        else if (boundary == 1) smokeCloseCallback(win.get());
        else win.reset();
    } catch (...) { escaped = true; }
    check("close smoke: exit, all protected hooks, detach, cleanup, lifetime and context",
          !escaped && state.exits == 1 && state.cleanups == 1 && state.after == 2 &&
          state.destroyed == 1 && state.detached && state.slotsStable &&
          state.destructorContext && lifetime.expired() && internal::currentWindowCtx() == previous);
    if (win) {
        win->finish();
        check("close smoke: retry inert, queue drained, bindings disconnected",
              trussc::internal::WindowRequestAccess::context(*win).asyncScreenshots.pending.empty() && trussc::internal::WindowRequestAccess::context(*win).appEventListeners.empty() &&
              state.exits == 1 && state.cleanups == 1 && state.after == 2);
    }
}
void closeSmoke() {
    auto savedRun = internal::appRunToken();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    for (int boundary = 0; boundary < 3; ++boundary) smokeClose(boundary, true, 0);
    // App exit/cleanup and both event bands retain exception isolation.
    for (int throwing = 1; throwing <= 4; ++throwing) smokeClose(1, true, throwing);
    internal::appRunToken() = std::move(savedRun);
}
void callbackFailure() {
    TestWindow win;
    auto* previous = internal::currentWindowCtx();
    tcCoreTest::nativeBoundary([&] {
        internal::WindowEntryScope dispatch(win);
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
    if (mode == "all" || mode == "sweep") { closeSmoke(); callbackFailure(); }
#endif
    std::printf("allocation regressions: %d failures\n", failures);
    return failures ? 1 : 0;
}
