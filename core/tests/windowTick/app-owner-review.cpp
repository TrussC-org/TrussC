// Production-linked regressions for C1 follow-up 5; no native/GPU stubs.
#include <TrussC.h>
#include <barrier>
#include <cstdio>
#include <thread>
using namespace tc;
// Observe cancellation while retaining the real native implementation.
static int quitCancellations = 0;
extern "C" void __real_sapp_cancel_quit(void);
extern "C" void __wrap_sapp_cancel_quit(void) {
    ++quitCancellations;
    __real_sapp_cancel_quit();
}
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("app-owner-review: %s: %s\n", label, ok ? "PASS" : "FAIL");
    failures += !ok;
}
struct Empty : App {};
struct Load : internal::ResourceLoadState {};
std::shared_ptr<Window> window(unsigned id) {
    auto value = internal::prepareLaunchWindow(WindowSettings{});
    internal::WindowRequestAccess::bindForTests(*value, id);
    return value;
}
size_t cancellations(const std::shared_ptr<internal::AppOwner>& owner) {
    std::lock_guard lock(owner->cancellationsMutex);
    return owner->cancellations.size();
}
std::string request(const char* tool, json args = json::object()) {
    return json{{"jsonrpc","2.0"},{"id",239},{"method","tools/call"},
        {"params",{{"name",tool},{"arguments",args}}}}.dump();
}
auto queue(const char* tool, json args = json::object()) {
    auto response = std::make_shared<std::promise<mcp::internal::ReplyThunk>>();
    auto future = response->get_future();
    mcp::internal::getHttpChannel().send(mcp::McpRequest{request(tool, args), response});
    mcp::processHttpQueue();
    return future;
}
void frame(Window& target) {
    auto now = std::chrono::steady_clock::now();
    mcp::internal::drainDeferredResponses(nullptr, false, now);
    mcp::internal::drainDeferredResponses(&target.context(), true, now, false);
    mcp::internal::drainDeferredResponses(nullptr, false, now);
}
json reply(std::future<mcp::internal::ReplyThunk>& future) {
    if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        throw std::runtime_error("reply not ready after production drain");
    return json::parse(std::async(std::launch::async, future.get()).get());
}
void delivery() {
    auto app = internal::makeConstructedApp<Empty>();
    auto owner = internal::appOwner(*app);
    Event<int> typed;
    Event<void> plain;
    EventListener a, b;
    int calls = 0;
    {
        internal::AppOwnerScope scope(owner);
        a = typed.listen([&](int&) { ++calls; }, Deliver::Main);
        b = plain.listen([&] { ++calls; }, Deliver::Main);
    }
    auto initial = cancellations(owner);
    std::barrier start(3);
    auto notify = [&] {
        start.arrive_and_wait();
        for (int i = 0; i < 500; ++i) { int value = i; typed.notify(value); plain.notify(); }
    };
    std::thread first(notify), second(notify);
    start.arrive_and_wait();
    first.join(); second.join();
    internal::drainMainThreadQueue();
    check("two workers deliver 1000 typed and 1000 void messages", calls == 2000);
    check("deliveries leave cancellation count unchanged", cancellations(owner) == initial);
    {
        internal::AppOwnerScope scope(owner);
        internal::ResourceLoadSlot<Load> slot;
        Event<LoadResult> done;
        for (int i = 0; i < 1000; ++i) slot.begin(done, [](Load&) {});
    }
    check("1000 loads leave cancellation count unchanged", cancellations(owner) == initial);
    Event<void> inlineMessages;
    EventListener inlineListener;
    int submitted = 0;
    {
        internal::AppOwnerScope scope(owner);
        inlineListener = inlineMessages.listen([&] { runOnMainThread([&] { ++submitted; }); });
    }
    auto beforeSubmissions = cancellations(owner);
    std::thread inlineWorker([&] { for (int i = 0; i < 1000; ++i) inlineMessages.notify(); });
    inlineWorker.join();
    internal::drainMainThreadQueue();
    check("1000 Inline listener submissions complete", submitted == 1000);
    check("completed single-argument submissions leave cancellation count unchanged", cancellations(owner) == beforeSubmissions);
    // A throwing first closure discards the rest of the drain's batch.
    // Neither invoked nor discarded submissions may register cancellation.
    Event<void> malformed;
    int discardedCalls = 0, caught = 0, drops = 0;
    Event<void> destructionEvent;
    int destructionNotifications = 0;
    auto destructionListener = destructionEvent.listen([&] { ++destructionNotifications; });
    struct Capture {
        std::shared_ptr<internal::AppOwner> owner;
        Event<void>& event;
        int& drops;
        ~Capture() {
            bool unlocked = owner->cancellationsMutex.try_lock();
            if (unlocked) owner->cancellationsMutex.unlock();
            check("queued capture destruction is outside owner mutex", unlocked);
            // Reenter Event registration and dispatch from capture destruction.
            auto listener = event.listen([] {});
            event.notify();
            ++drops;
        }
    };
    EventListener malformedListener;
    {
        internal::AppOwnerScope scope(owner);
        malformedListener = malformed.listen([&] {
            runOnMainThread([] { throw std::runtime_error("malformed content"); });
            auto capture = std::shared_ptr<Capture>(new Capture{owner, destructionEvent, drops});
            runOnMainThread([&, capture] { ++discardedCalls; });
        });
    }
    auto beforeMalformed = cancellations(owner);
    for (int i = 0; i < 1000; ++i) {
        std::thread worker([&] { malformed.notify(); }); worker.join();
        try { internal::drainMainThreadQueue(); }
        catch (const std::runtime_error&) { ++caught; }
    }
    check("1000 malformed+ok Inline submission pairs leave cancellation list unchanged",
          caught == 1000 && discardedCalls == 0 && cancellations(owner) == beforeMalformed &&
          internal::mainThreadQueue().empty());
    check("discard destroys all captures outside Event mutex", drops == 1000 && destructionNotifications == 1000);
    for (int count : {5000, 10000, 20000, 40000}) {
        int ran = 0;
        std::thread worker([&] {
            internal::AppOwnerScope scope(owner);
            for (int i = 0; i < count; ++i) runOnMainThread([&] { ++ran; });
        }); worker.join();
        check("pending owned submissions leave cancellation list unchanged", cancellations(owner) == beforeMalformed);
        const auto start = std::chrono::steady_clock::now();
        internal::drainMainThreadQueue();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("owned-drain: queued=%d ran=%d seconds=%.9f\n", count, ran, seconds);
        check("burst submissions complete without cancellation traffic", ran == count && cancellations(owner) == beforeMalformed);
    }
    std::thread successfulCapture([&] {
        internal::AppOwnerScope scope(owner);
        auto held = std::shared_ptr<Capture>(new Capture{owner, destructionEvent, drops});
        runOnMainThread([held] {});
    }); successfulCapture.join();
    internal::drainMainThreadQueue();
    check("successful queue destruction releases capture outside locks", drops == 1001 && destructionNotifications == 1001);
    std::weak_ptr<Capture> queuedCapture;
    {
        internal::AppOwnerScope scope(owner);
        auto held = std::shared_ptr<Capture>(new Capture{owner, destructionEvent, drops}); queuedCapture = held;
        std::thread worker([&, held] {
            internal::AppOwnerScope scope(owner);
            runOnMainThread([&, held] { ++submitted; });
        });
        worker.join();
    }
    internal::ResourceLoadSlot<Load> pendingLoad;
    Event<LoadResult> loaded;
    int applied = 0;
    {
        internal::AppOwnerScope scope(owner);
        auto state = pendingLoad.begin(loaded, [&](Load&) { ++applied; });
        internal::completeResourceLoad(state);
    }
    std::thread queued([&] { int value = 0; typed.notify(value); plain.notify(); });
    queued.join();
    internal::retireAppOwner(owner);
    check("single-argument queue retains captures until closure destruction", !queuedCapture.expired());
    std::thread retiredSubmission([&] {
        internal::AppOwnerScope scope(owner);
        auto held = std::shared_ptr<Capture>(new Capture{owner, destructionEvent, drops});
        runOnMainThread([&, held] { ++submitted; }); inlineMessages.notify();
    });
    retiredSubmission.join();
    internal::drainMainThreadQueue();
    check("pending and subsequent single-argument work refuses retirement", submitted == 1000);
    check("queue destruction releases retired and rejected captures outside locks", queuedCapture.expired() && drops == 1003 && destructionNotifications == 1003);
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    check("resource completion after retirement does not apply", applied == 0);
    int value = 0; typed.notify(value); plain.notify();
    check("queued and subsequent delivery after retirement runs nothing", calls == 2000);

    // Race actual registration with retirement. Both Event specializations
    // must publish/disconnect outside the Event lock and finish without a hang.
    for (int round = 0; round < 100; ++round) {
        auto candidate = internal::makeConstructedApp<Empty>();
        auto live = internal::appOwner(*candidate);
        std::barrier race(2);
        std::thread registerWorker([&] {
            internal::AppOwnerScope scope(live);
            race.arrive_and_wait();
            for (int i = 0; i < 20; ++i) {
                auto x = typed.listen([](int&) {}, Deliver::Main);
                auto y = plain.listen([] {}, Deliver::Main);
            }
        });
        race.arrive_and_wait();
        internal::retireAppOwner(live);
        registerWorker.join();
        check("concurrent registration/retirement empties cancellation list", cancellations(live) == 0);
    }
}
void survivingQuitReceiver() {
    auto a = window(301), b = window(302);
    a->setApp<Empty>(); b->setApp<Empty>();
    internal::applyPendingAppForTests(*a); internal::applyPendingAppForTests(*b);
    int aCalls = 0, bCalls = 0;
    EventListener first, last;
    {
        internal::AppOwnerScope scope(internal::appOwner(*a->getApp()));
        first = a->events().exitRequested.listen([&](ExitRequestEventArgs&) { ++aCalls; });
    }
    {
        internal::AppOwnerScope scope(internal::appOwner(*b->getApp()));
        last = b->events().exitRequested.listen([&](ExitRequestEventArgs& args) {
            ++bCalls; args.cancel = true;
            check("Runtime quit retains native busy and Window dispatch guards", internal::hostCallbacksActive() && internal::WindowRequestAccess::dispatchActive(*b));
            check("surviving quit callback restores its App owner", internal::currentAppOwner() == internal::appOwner(*b->getApp()));
        });
    }
    a->close(); internal::WindowRequestAccess::teardown(*a);
    sapp_event event{}; event.type = SAPP_EVENTTYPE_QUIT_REQUESTED;
    auto* caller = internal::currentWindowCtx();
    internal::currentWindowCtx() = &b->context(); // An already-selected receiver still needs admission.
    internal::windowEvent(&event, {0}, nullptr);
    check("Runtime quit restores the caller Window selection", internal::currentWindowCtx() == &b->context());
    internal::currentWindowCtx() = caller;
    check("Runtime asks surviving B after first-created A closes", aCalls == 0 && bCalls == 1);
    b->close(); internal::WindowRequestAccess::teardown(*b);
}
void throwingQuitStacks() {
    auto target = window(401);
    target->setApp<Empty>(); internal::applyPendingAppForTests(*target);
    EventListener listener;
    int calls = 0;
    {
        internal::AppOwnerScope scope(internal::appOwner(*target->getApp()));
        listener = target->events().exitRequested.listen([&](ExitRequestEventArgs&) {
            ++calls;
            pushStyle(); setColor(1.f, 0.f, 0.f);
            pushMatrix(); scale(3.f);
            throw std::runtime_error("quit confirmation failed");
        });
    }
    sapp_event event{}; event.type = SAPP_EVENTTYPE_QUIT_REQUESTED;
    auto inspect = [&](const char* label) {
        internal::WindowContextScope selection(&target->context());
        auto& rc = internal::getDefaultContext();
        const auto color = rc.getColor();
        std::printf("quitguard: %s style=%zu matrix=%zu color=%.1f,%.1f,%.1f scale=%.1f\n",
                    label, rc.getStyleStackDepth(), rc.getMatrixStackDepth(), color.r, color.g, color.b, rc.getScale());
        check(label, rc.getStyleStackDepth() == 0 && rc.getMatrixStackDepth() == 0 &&
              color.r == 1 && color.g == 1 && color.b == 1 && rc.getScale() == 1);
    };
    { internal::WindowContextScope selection(&target->context()); setColor(1.f, 1.f, 1.f); }
    internal::dispatchWindowEventForTests(&event, *target);
    inspect("addressed quit restores stacks");
    const int beforeCancel = quitCancellations;
    internal::windowEvent(&event, {0}, nullptr);
    inspect("Runtime quit restores stacks");
    check("throwing quit is vetoed and keeps App and Window live", quitCancellations == beforeCancel + 1 && calls == 2 && target->isOpen() && bool(target->getApp()));
    target->close(); internal::WindowRequestAccess::teardown(*target);
}
void deferrals() {
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::tool("single", "").bind([]() -> json {
        mcp::deferToolResultUntilAfterFrame([]() -> json { return true; }); return nullptr;
    });
    mcp::tool("double", "").bind([]() -> json {
        mcp::deferToolResultTwoStage([] { return []() -> json { return true; }; }); return nullptr;
    });
    for (auto tool : {"single", "double"}) {
        auto f = queue(tool);
        check("zero-window omission errors at DEFER time", reply(f)["result"].value("isError", false));
    }
    {
        auto headless = internal::makeConstructedAppAt<Empty>(&internal::nonWindowContext());
        internal::AppOwnerScope scope(internal::appOwner(*headless));
        bool rejected = false;
        try { mcp::deferToolResultUntilAfterFrame([]() -> json { return true; }); }
        catch (const std::runtime_error&) { rejected = true; }
        check("headless omission fails at DEFER time", rejected);
        mcp::internal::resetDeferralState();
    }
    auto target = window(101);
    for (auto tool : {"single", "double"}) {
        auto f = queue(tool);
        mcp::internal::drainDeferredResponses(nullptr, false);
        check("maintenance cannot produce an unpresented reply", f.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
        frame(*target);
        check("sole-window default deferral completes", !reply(f)["result"].value("isError", false));
    }
    int getters = 0;
    mcp::statusImage("review", [&] { ++getters; Pixels pixels; pixels.allocate(2, 2, 4); return pixels; });
    auto status = queue("tc_get_status_image", {{"name","review"}});
    frame(*target);
    auto statusResult = reply(status);
    check("status image executes with sole Window", getters == 1 && !statusResult["result"].value("isError", false) && statusResult["result"]["content"][0]["type"] == "image");
    Fbo fbo;
    fbo.setDebugName("review-fbo");
    auto analyze = queue("tc_analyze_image", {{"source",{{"fbo","review-fbo"}}},{"ops",json::array({{{"op","stats"}}})}});
    check("Fbo branch defers before readback", analyze.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
    frame(*target);
    check("Fbo branch reaches producer with sole Window", !reply(analyze)["result"].value("isError", false));

    auto peer = window(202);
    for (auto tool : {"single", "double"}) {
        auto f = queue(tool);
        auto result = reply(f);
        check("ambiguous omission errors immediately with positive IDs", result["result"].value("isError", false) && result.dump().find("101") != std::string::npos && result.dump().find("202") != std::string::npos);
    }
    target->setApp<Empty>(); internal::applyPendingAppForTests(*target);
    target->context().fbWidth = 640;
    auto owner = internal::appOwner(*target->getApp());
    Event<void> lazy;
    std::vector<EventListener> listeners;
    int mainCalls = 0, workerCalls = 0, lazyCalls = 0;
    {
        internal::AppOwnerScope scope(owner);
        mcp::tool("owned-single", "").bind([&]() -> json {
            mcp::deferToolResultUntilAfterFrame([&]() -> json {
                check("single stage restores App APIs with two Windows", internal::currentAppOwner() == owner && getFramebufferWidth() == 640);
                listeners.push_back(lazy.listen([&] { ++lazyCalls; }));
                mcp::tool("lazy-tool", "").bind([] { return true; });
                ++mainCalls; return true;
            }); return nullptr;
        });
        mcp::tool("owned-double", "").bind([&]() -> json {
            mcp::deferToolResultTwoStage([&]() -> std::function<json()> {
                check("two-stage main restores App APIs", internal::currentAppOwner() == owner && getFramebufferWidth() == 640);
                ++mainCalls;
                return [&]() -> json {
                    check("worker stage restores App APIs and registration owner", !isMainThread() && internal::currentAppOwner() == owner && getFramebufferWidth() == 640 && mcp::internal::registrationOwner() == owner->token.get());
                    listeners.push_back(lazy.listen([&] { ++lazyCalls; }));
                    ++workerCalls; return true;
                };
            }, &target->context()); return nullptr;
        });
    }
    for (auto tool : {"owned-single", "owned-double"}) {
        auto f = queue(tool);
        frame(*peer);
        check("App deferral waits for its own Window", f.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
        frame(*target);
        check("App stages return successful reply", !reply(f)["result"].value("isError", false));
        check("caller attribution restored", !internal::currentAppOwner());
    }
    lazy.notify();
    check("both deferred stages register owned listeners", lazyCalls == 2 && mainCalls == 2 && workerCalls == 1);
    auto singlePending = queue("owned-single");
    frame(*target);
    auto singleThunk = singlePending.get();
    auto workerPending = queue("owned-double");
    frame(*target);
    auto workerThunk = workerPending.get();
    check("worker stage can remain pending after main stage", mainCalls == 4 && workerCalls == 1);
    auto retired = queue("owned-single");
    internal::retireAppOwner(owner);
    for (auto* thunk : {&singleThunk, &workerThunk}) {
        auto wire = json::parse(std::async(std::launch::async, std::move(*thunk)).get());
        check("retired delivery preserves JSON-RPC version and original request id", wire.value("jsonrpc", "") == "2.0" && wire["id"] == 239);
        check("retired delivery uses existing tool-error envelope", wire["result"].value("isError", false) &&
            wire["result"]["content"][0]["type"] == "text" && wire["result"]["content"][0]["text"].get<std::string>().find("retired") != std::string::npos);
    }
    check("retired worker stage runs no App code", workerCalls == 1);
    frame(*target);
    check("retired deferred producer does not run", reply(retired)["result"].value("isError", false) && mainCalls == 4);
    lazy.notify();
    check("deferred-stage listeners retire with App", lazyCalls == 2 && lazy.listenerCount() == 0);
    auto missing = json::parse(mcp::internal::Server::instance().processMessage(request("lazy-tool")));
    check("deferred-stage tool retires with App", missing.contains("error"));
}
}
int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::puts("app-owner-review: main entered");
    getMainThreadId(); internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    try { delivery(); survivingQuitReceiver(); throwingQuitStacks(); deferrals(); }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); ++failures; }
    internal::shutdownRuntime();
    std::printf("app-owner-review: %d failures\n", failures);
    return failures ? 1 : 0;
}
