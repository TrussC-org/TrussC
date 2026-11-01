// Constructor ownership, stable attachment slots and the shared normal ending.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <stdexcept>
using namespace tc;
namespace {
int failures = 0;
void check(const char* name, bool okay) {
    std::printf("windowSetApp: %s: %s\n", name, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
Event<void> appSignal;
std::vector<std::string> trace;
struct State {
    int setups = 0, updates = 0, exits = 0, cleanups = 0, calls = 0, constructorUpdates = 0;
    internal::WindowContext* constructorContext = nullptr;
    std::shared_ptr<std::atomic<bool>> token;
    const void* owner = nullptr;
    Window* window = nullptr;
    bool detached = false;
};
struct Probe : App {
    State& state;
    std::string name;
    EventListener listener, constructorListener;
    std::shared_ptr<Window> self;
    bool throwExit = false, throwCleanup = false;
    Probe(State& s, std::string n) : state(s), name(std::move(n)) {
        state.token = internal::appRunToken();
        state.owner = mcp::internal::registrationOwner();
        state.constructorContext = internal::currentAppOwner()->context;
        constructorListener = events().update.listen([this] { ++state.constructorUpdates; });
        listener = appSignal.listen([this] {
            ++state.calls;
            mcp::tool(name + ":lazy", "lazy registration").bind([]() -> json { return true; });
        });
        mcp::tool(name, "lifetime probe").bind([this]() -> int { return state.calls; });
    }
    void setup() override {
        ++state.setups;
        state.window = getWindow();
        check("size seeded before setup", getWidth() == 320 && getHeight() == 200);
    }
    void update() override { ++state.updates; trace.push_back(name + ":update"); }
    void exit() override {
        ++state.exits;
        state.detached = !getWindow() && !getRootNode();
        trace.push_back(name + ":exit");
        if (internal::runtimeEnding()) exitApp(99); // harmless during Runtime shutdown
        if (throwExit) throw std::runtime_error("exit probe");
    }
    void cleanup() override {
        ++state.cleanups;
        check("audio detached before cleanup", !internal::appOwner(*this)->retiring && internal::appRanCleanup(*this));
        trace.push_back(name + ":cleanup");
        if (throwCleanup) throw std::runtime_error("cleanup probe");
    }
};
std::shared_ptr<Window> window(uint32_t id) {
    auto result = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*result, id);
    trussc::internal::WindowRequestAccess::context(*result).fbWidth = 320;
    trussc::internal::WindowRequestAccess::context(*result).fbHeight = 200;
    trussc::internal::WindowRequestAccess::context(*result).dpiScale = 1;
    internal::retainRuntimeWindow(result);
    return result;
}
void apply(Window& w) { internal::applyPendingAppForTests(w); }
void tick(Window& w) {
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &trussc::internal::WindowRequestAccess::context(w);
    { internal::WindowEntryScope entry(w); if (entry) w.events().update.notify(); }
    internal::currentWindowCtx() = previous;
}
bool nakedBody = false;
struct Naked : App { Naked() { nakedBody = true; } };
struct NestedNaked : App { NestedNaked() { Naked child; } };
struct EarlierNaked { EarlierNaked() { Naked child; } };
struct EarlierParent : EarlierNaked, App {};
struct EarlierMember { Naked child; };
struct MemberParent : EarlierMember, App {};
struct DestructorProbe final : App {
    State& state;
    explicit DestructorProbe(State& s) : state(s) {
        state.token = internal::appRunToken();
    }
    ~DestructorProbe() override {
        auto owner = internal::currentAppOwner();
        state.detached = owner && owner->retiring && !owner->token->load() && internal::appRunToken() == state.token;
        runOnMainThread([&state = state] { ++state.calls; });
        mcp::tool("retired-destructor", "must be refused").bind([]() -> json { return true; });
    }
};
struct Throwing : App {
    EventListener listener;
    Throwing(State& state) {
        state.token = internal::appRunToken();
        listener = appSignal.listen([&state] { ++state.calls; });
        mcp::tool("throwing-candidate", "throw").bind([]() -> json { return true; });
        throw std::runtime_error("constructor probe");
    }
};
struct Nested : Probe {
    Nested(State& a, State& b, Window& other, std::string name) : Probe(a, std::move(name)) {
        auto before = internal::appRunToken();
        other.setApp<Probe>(b, "nested-B");
        check("nested constructor restores A token and MCP owner", internal::appRunToken() == before && mcp::internal::registrationOwner() == a.owner);
    }
};
struct VirtualProbe : App {
    static int childUpdates, childCleanups, exits, cleanups;
    struct Child : Node {
        void update() override { ++childUpdates; }
        void cleanup() override { ++childCleanups; }
    };
    void setup() override { addChild(std::make_shared<Child>()); }
    void update() override { requestExit(); }
    void exit() override { ++exits; check("headless ending has no Window or root", !getWindow() && !getRootNode()); }
    void cleanup() override { ++cleanups; }
};
int VirtualProbe::childUpdates = 0, VirtualProbe::childCleanups = 0, VirtualProbe::exits = 0, VirtualProbe::cleanups = 0;
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::beginRuntimeRun();
    internal::beginFrameworkLifetime();
    try { Naked object; check("factory-only constructor throws", false); }
    catch (const std::logic_error&) { check("factory rejection precedes derived body", !nakedBody); }
    try { auto parent = internal::makeConstructedApp<NestedNaked>(); check("nested naked constructor throws", false); }
    catch (const std::logic_error&) { check("nested rejection precedes child body", !nakedBody); }
    nakedBody = false;
    try { auto parent = internal::makeConstructedApp<EarlierParent>(); check("earlier-base naked constructor throws", false); }
    catch (const std::logic_error&) { check("earlier-base child cannot steal factory allocation", !nakedBody); }

    auto invalidSubscription = events().update.listen([] {});
    check("no-window omission has no subscription or dimensions", !invalidSubscription && getDpiScale() == 0 && getWindowWidth() == 0);
    auto a = window(101), b = window(202);
    nakedBody = false;
    try { a->setApp<MemberParent>(); check("App member composition fails outer construction", false); }
    catch (const std::logic_error& error) {
        check("known range gap admits member body but rejects outer App",
              nakedBody && std::string(error.what()).find("App construction requires") != std::string::npos && !a->getApp());
    }
    check("quit-on-close defaults false", !WindowSettings{}.quitOnClose && !internal::WindowRequestAccess::quitOnClose(*a));
    a->setQuitOnClose(true);
    check("quit-on-close live setter updates this Window", internal::WindowRequestAccess::quitOnClose(*a) && !internal::WindowRequestAccess::quitOnClose(*b));
    a->setQuitOnClose(false);
    State first, second, discarded, throwing;
    a->setApp<Probe>(first, "A"); apply(*a); tick(*a);
    auto firstApp = std::dynamic_pointer_cast<Probe>(a->getApp());
    try { a->setApp<Throwing>(throwing); check("throw propagates", false); }
    catch (const std::runtime_error&) {}
    check("throw preserves current attachment", a->getApp() == firstApp);
    check("throw retires only candidate token and tool", throwing.token && !throwing.token->load() && first.token->load() && !mcp::hasTool("throwing-candidate"));
    appSignal.notify();
    check("lazy registration inherits A owner", mcp::hasTool("A:lazy"));
    b->setApp<Probe>(second, "B"); apply(*b); tick(*b);
    auto foreignNode = std::make_shared<RectNode>(); firstApp->addChild(foreignNode);
    auto ownNode = std::make_shared<RectNode>(); b->getApp()->addChild(ownNode);
    trussc::internal::WindowRequestAccess::context(*b).hoveredNode = foreignNode; trussc::internal::WindowRequestAccess::context(*b).grabbedNode = foreignNode;
    trussc::internal::WindowRequestAccess::context(*b).grabbedButton = 0; trussc::internal::WindowRequestAccess::context(*b).selectedNode = ownNode;
    a->setApp<Probe>(discarded, "discarded");
    a->destroyApp(); apply(*a);
    check("unpublished candidate has no invented lifecycle", discarded.setups == 0 && discarded.exits == 0 && discarded.cleanups == 0 && !discarded.token->load());
    check("destroy detaches root and App before ending", !a->getApp() && first.detached && first.exits == 1 && first.cleanups == 1 && a->isOpen());
    check("ending clears foreign tree refs and preserves B's live selection",
          trussc::internal::WindowRequestAccess::context(*b).hoveredNode.expired() && trussc::internal::WindowRequestAccess::context(*b).grabbedNode.expired() &&
          trussc::internal::WindowRequestAccess::context(*b).grabbedButton == -1 && trussc::internal::WindowRequestAccess::context(*b).selectedNode.lock() == ownNode &&
          trussc::internal::WindowRequestAccess::context(*b).rootNode.lock() == b->getApp());
    check("A ending preserves B tools and token", !first.token->load() && second.token->load() && !mcp::hasTool("A") && !mcp::hasTool("A:lazy") && mcp::hasTool("B"));
    const int oldCalls = first.calls;
    appSignal.notify();
    check("retired subscriptions cannot run", first.calls == oldCalls && second.calls == 1);
    firstApp.reset();
    State nestedA, nestedB;
    a->setApp<Nested>(nestedA, nestedB, *b, "nested-A"); apply(*a); apply(*b); tick(*a); tick(*b);
    check("nested candidate owns an independent token", nestedA.token != nestedB.token && nestedA.owner != nestedB.owner);
    check("constructor events select the receiving Window outside a callback", nestedA.constructorContext == &trussc::internal::WindowRequestAccess::context(*a) && nestedB.constructorContext == &trussc::internal::WindowRequestAccess::context(*b) && nestedA.constructorUpdates == 1 && nestedB.constructorUpdates == 1);
    {
        internal::AppOwnerScope scope(internal::appOwner(*a->getApp()));
        State callbackA, callbackB;
        a->setApp<Nested>(callbackA, callbackB, *b, "callback-A");
        check("constructor inside A callback restores caller owner", internal::appRunToken() == nestedA.token);
        a->destroyApp(); b->destroyApp();
    }
    apply(*a); apply(*b);
    State stable;
    a->setApp<Probe>(stable, "stable"); apply(*a);
    std::vector<std::string> observed;
    auto equal = a->events().update.listen([&] { observed = trace; }, EventPriority::App);
    trace.clear(); tick(*a);
    check("App slot stays before later equal-priority listeners", observed == std::vector<std::string>{"stable:update"});
    State pending;
    a->setApp<Probe>(pending, "pending-close");
    a->close(); apply(*a);
    check("close cancels pending replacement", !pending.token->load() && pending.exits == 0 && pending.setups == 0);
    internal::WindowRequestAccess::teardown(*a);
    internal::WindowRequestAccess::teardown(*a);
    check("explicit close ends once and leaves B open", stable.exits == 1 && stable.cleanups == 1 && !a->isOpen() && b->isOpen());
    State workerState;
    b->setApp<Probe>(workerState, "worker-B"); apply(*b); tick(*b);
    Event<void> workerSignal;
    EventListener workerListener;
    std::weak_ptr<int> queuedCapture;
    int workerCalls = 0;
    {
        internal::AppOwnerScope scope(internal::appOwner(*b->getApp()));
        auto held = std::make_shared<int>(1);
        queuedCapture = held;
        workerListener = workerSignal.listen([&, held] { ++workerCalls; }, Deliver::Main);
    }
    std::thread delivery([&] { workerSignal.notify(); });
    delivery.join();
    b->destroyApp(); apply(*b);
    check("retirement releases queued callback captures before a drain", queuedCapture.expired());
    internal::drainMainThreadQueue();
    check("retired worker delivery cannot invoke App code", workerCalls == 0);
    auto c = window(303);
    State lastB, lastC;
    b->setApp<Probe>(lastB, "last-B"); c->setApp<Probe>(lastC, "last-C");
    apply(*b); apply(*c); tick(*b); tick(*c);
    std::dynamic_pointer_cast<Probe>(b->getApp())->self = b;
    std::dynamic_pointer_cast<Probe>(c->getApp())->throwExit = true;
    std::dynamic_pointer_cast<Probe>(c->getApp())->throwCleanup = true;
    std::weak_ptr<App> weakB = b->getApp();
    auto* retained = c.get();
    std::weak_ptr<Window> weakC = c;
    c.reset();
    check("dropping every external handle leaves Window live", !weakC.expired() && retained->isOpen());
    State retainedState;
    auto retainedWindow = window(404);
    retainedWindow->setApp<DestructorProbe>(retainedState); apply(*retainedWindow);
    auto retainedApp = retainedWindow->getApp();
    retainedWindow->destroyApp(); apply(*retainedWindow);
    trace.clear(); internal::shutdownRuntime();
    check("shutdown uses one reverse attachment snapshot despite throws", trace == std::vector<std::string>{"last-C:exit", "last-C:cleanup", "last-B:exit", "last-B:cleanup"});
    check("shutdown cuts self-held handle cycles and retires owners", weakB.expired() && !lastB.token->load() && !lastC.token->load() && !b->isOpen());
    internal::endFrameworkLifetime();
    const auto oldId = internal::WindowRequestAccess::creationId(*b);
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    b->close(); b->setTitle("old epoch");
    retainedApp.reset();
    check("late derived destructor keeps its retired owner in a new run", retainedState.detached && retainedState.calls == 0 && !mcp::hasTool("retired-destructor"));
    check("late App destruction restores the new Runtime token", internal::appRunToken() != retainedState.token && internal::appRunToken()->load());
    check("late handle remains closed in a new run", !b->isOpen() && internal::WindowRequestAccess::creationId(*b) == oldId);
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    check("headless launcher uses the common ending", runHeadlessApp<VirtualProbe>() == 0 && VirtualProbe::exits == 1 && VirtualProbe::cleanups == 1);
    check("headless remains virtual-only", VirtualProbe::childUpdates == 0 && VirtualProbe::childCleanups == 0);
    return failures ? 1 : 0;
}
