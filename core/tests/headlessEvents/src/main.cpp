// #947: real headless event delivery (based on the fc15145e fixture).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <limits>
#include <cstdio>
#include <string>
#include <thread>

using namespace tc;

namespace {
// Whitebox subscriptions explicitly target the headless hub before launch.
CoreEvents& fixtureEvents() { return *internal::nonWindowContext().coreEvents; }

int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

enum class Mode { Order, ClearBeforeApp, ClearInApp };
Mode mode;
std::string order;
int updates, exits, cleanups, teardowns, childUpdates, modUpdates, timerCalls;
std::thread::id mainThread;
struct Child : Node { void update() override { ++childUpdates; } };
struct ProbeMod : Mod { void update() override { ++modUpdates; } };

struct EventsApp : App {
    std::vector<EventListener> listeners;
    void setup() override {
        addChild(std::make_shared<Child>());
        addMod<ProbeMod>();
        callAfter(0, [] { ++timerCalls; });
        listeners.push_back(fixtureEvents().update.listen([] { order += 'c'; }, EventPriority::App));
        listeners.push_back(fixtureEvents().update.listen([] { order += 'z'; }, EventPriority::AfterApp));
        listeners.push_back(fixtureEvents().exit.listen([] { order += 'C'; }, EventPriority::App));
        listeners.push_back(fixtureEvents().exit.listen([] { order += 'Z'; }, EventPriority::AfterApp));
        // Exercise the same protected, self-disconnecting ownership as mandatory
        // recorder/ImGui/inspector teardown. A live inspector is tested in its addon.
        listeners.push_back(internal::FrameworkEventAccess::listen(fixtureEvents().exit, [this] {
            check("mandatory teardown runs after App exit and before cleanup", exits == 1 && cleanups == 0);
            order += 'T';
            ++teardowns;
            listeners.back().disconnect();
        }, EventPriority::AfterApp));
    }
    void update() override {
        check("headless App update stays on main thread", std::this_thread::get_id() == mainThread);
        order += 'a';
        if (++updates == 3) exitApp(7);
    }
    void exit() override {
        check("headless App exit stays on main thread", std::this_thread::get_id() == mainThread);
        order += 'A';
        ++exits;
        if (mode == Mode::ClearInApp) fixtureEvents().exit.clear();
    }
    void cleanup() override { order += 'K'; ++cleanups; }
};

void testDelivery(Mode requested) {
    mode = requested;
    order.clear();
    updates = exits = cleanups = teardowns = childUpdates = modUpdates = timerCalls = 0;
    const auto updateCount = fixtureEvents().update.listenerCount();
    const auto exitCount = fixtureEvents().exit.listenerCount();
    auto defaultUpdate = fixtureEvents().update.listen([] {
        order += 'd';
        if (mode == Mode::ClearBeforeApp) fixtureEvents().update.clear();
    });
    auto beforeUpdate = fixtureEvents().update.listen([] { order += 'b'; }, EventPriority::App);
    auto defaultExit = fixtureEvents().exit.listen([] {
        order += 'D';
        if (mode == Mode::ClearBeforeApp) fixtureEvents().exit.clear();
    });
    auto beforeExit = fixtureEvents().exit.listen([] { order += 'B'; }, EventPriority::App);
    check("runHeadlessApp returns exit code", runHeadlessApp<EventsApp>(HeadlessSettings().setFps(1024)) == 7);
    check("App update, exit and cleanup survive clear and run exactly once per delivery",
          updates == 3 && exits == 1 && cleanups == 1 && teardowns == 1);
    const std::string expected = mode == Mode::Order ? "dbaczdbaczdbaczDBACZTK"
        : mode == Mode::ClearBeforeApp ? "daaaDATK" : "dbaczdbaczdbaczDBATK";
    std::printf("order: %s\n", order.c_str());
    check("default, tied 100, App, tied 100, AfterApp order / clear during notify", order == expected);
    check("headless updates children, Mods and timers",
          childUpdates == 3 && modUpdates == 3 && timerCalls == 1);
    defaultUpdate.disconnect(); beforeUpdate.disconnect();
    defaultExit.disconnect(); beforeExit.disconnect();
    check("runner disconnects App bindings after returning",
          fixtureEvents().update.listenerCount() == updateCount && fixtureEvents().exit.listenerCount() == exitCount);
}

// Repost a slot to advance several idle passes without relying on a timer.
int idlePasses = 0, idleUpdates = 0;
struct IdleApp : App, internal::MainThreadSlot {
    IdleApp() : MainThreadSlot([](MainThreadSlot& slot) {
        auto& app = static_cast<IdleApp&>(slot);
        if (++idlePasses == 3) exitApp();
        else app.post(app.shared_from_this());
    }) {}
    void setup() override { post(shared_from_this()); }
    void update() override { ++idleUpdates; }
};
void testInvalidFps() {
    for (float fps : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity()}) {
        idlePasses = idleUpdates = 0;
        int warnings = 0;
        bool startupOnly = true;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Warning
                    && event.message.find("fps must be a finite positive rate (got ") != std::string::npos
                    && event.message.find("); update() will not run") != std::string::npos) {
                ++warnings;
                startupOnly = startupOnly && idlePasses == 0;
            }
        });
        check("invalid fps runner exits", runHeadlessApp<IdleApp>(HeadlessSettings().setFps(fps)) == 0);
        check("invalid fps warns exactly once at startup", warnings == 1 && startupOnly);
        check("invalid fps runs idle passes without updates", idlePasses == 3 && idleUpdates == 0);
    }
}

struct CatchUpApp : App {
    int steps = 0;
    void update() override { if (++steps == 3) exitApp(7); }
};
void testCatchUp() {
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    headless::active = headless::running = true;
    headless::quitRequested = false;
    headless::pendingSignal = headless::deliveredSignal = 0;
    headless::frameCount = 0;
    internal::appExitCode() = 0;
    internal::clearExitReason();
    auto app = internal::makeConstructedApp<CatchUpApp>();
    auto listeners = internal::registerHeadlessAppEventListeners(std::make_shared<std::weak_ptr<App>>(app));
    auto& ctx = internal::nonWindowContext();
    internal::recordHeadlessAttachment(app, ctx);
    ctx.rootNode = app;
    internal::HeadlessUpdateState state;
    // Binary-exact elapsed and interval: ten steps owed, regardless of load.
    const int savedSteps = getMaxUpdateSteps();
    setMaxUpdateSteps(10);
    internal::runHeadlessUpdatePass(*app, ctx, state, 10.0 / 128.0, 1.0 / 128.0);
    setMaxUpdateSteps(savedSteps);
    check("catch-up exitApp in step 3 prevents further update events",
          app->steps == 3 && headless::frameCount == 3 && !headless::running
          && internal::appExitCode() == 7);
    internal::shutdownRuntime();
    headless::active = false;
    internal::endFrameworkLifetime();
}

// #906: deterministic headless tree/timing behavior through the scheduled entry.
constexpr double treeDelta = 1.0 / 128.0;
std::chrono::steady_clock::time_point treePassTime;
struct TreeState {
    int steps = 0, childUpdates = 0, modUpdates = 0;
    int childTimerStep = 0, appTimerStep = 0;
    bool timingScope = true;
    std::weak_ptr<Node> destroyed;
    std::vector<std::string> cleanupOrder;
};
struct TreeChild : Node {
    TreeState& state;
    bool destroyOnUpdate;
    TreeChild(TreeState& s, bool destroyOnUpdate) : state(s), destroyOnUpdate(destroyOnUpdate) {}
    void update() override {
        if (destroyOnUpdate) destroy();
        else ++state.childUpdates;
    }
    void cleanup() override { state.cleanupOrder.push_back(destroyOnUpdate ? "destroyed" : "child"); }
};
struct TreeMod : Mod {
    TreeState& state;
    explicit TreeMod(TreeState& s) : state(s) {}
    void update() override { ++state.modUpdates; }
};
struct TreeApp : App {
    TreeState& state;
    std::shared_ptr<TreeChild> child;
    explicit TreeApp(TreeState& s) : state(s) {}
    void setup() override {
        child = std::make_shared<TreeChild>(state, false);
        addChild(child);
        auto doomed = std::make_shared<TreeChild>(state, true);
        state.destroyed = doomed;
        addChild(doomed);
        addMod<TreeMod>(state);
    }
    void update() override {
        ++state.steps;
        auto& ctx = internal::currentWindowContext();
        const auto expected = treePassTime - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>((3 - state.steps) * treeDelta));
        state.timingScope = state.timingScope && ctx.inUpdate && ctx.fixedStepUpdate
            && getDeltaTime() == treeDelta && ctx.updateTime == expected;
        if (state.steps == 1) {
            callAfter(2 * treeDelta, [this] { state.appTimerStep = state.steps + 1; });
            child->callAfter(treeDelta, [this] { state.childTimerStep = state.steps; });
        }
    }
    void cleanup() override { state.cleanupOrder.push_back("app"); }
};
void testTreeLifecycle() {
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    headless::active = headless::running = true;
    headless::quitRequested = false;
    headless::pendingSignal = headless::deliveredSignal = 0;
    TreeState result;
    auto& ctx = internal::nonWindowContext();
    auto app = internal::makeConstructedApp<TreeApp>(result);
    internal::recordHeadlessAttachment(app, ctx);
    ctx.rootNode = app;
    auto listeners = internal::registerHeadlessAppEventListeners(std::make_shared<std::weak_ptr<App>>(app));
    {
        internal::AppOwnerScope owner(internal::appOwner(*app));
        internal::setupNodeOnce(*app);
    }
    // Supply the pass clock, just as window fixed-step fixtures do. No sleeps
    // or elapsed-time thresholds determine these assertions.
    treePassTime = std::chrono::steady_clock::now();
    auto previousClock = internal::windowClockForTests();
    internal::windowClockForTests() = [] { return treePassTime; };
    internal::HeadlessUpdateState state;
    const bool previousInUpdate = ctx.inUpdate;
    internal::runHeadlessUpdatePass(*app, ctx, state, 3 * treeDelta, treeDelta);
    internal::windowClockForTests() = previousClock;
    check("headless scheduled steps share inUpdate, fixed-step and nominal updateTime",
          result.steps == 3 && result.timingScope && ctx.inUpdate == previousInUpdate);
    check("headless child and Mod each update once per scheduled step",
          result.childUpdates == 3 && result.modUpdates == 3);
    check("headless timers created inside a fixed step count from the next step",
          result.appTimerStep == 3 && result.childTimerStep == 2);
    check("headless destroyed child is swept and cleaned on the next step",
          result.destroyed.expired() && result.cleanupOrder == std::vector<std::string>{"destroyed"});
    internal::shutdownRuntime();
    check("headless child cleanup runs before App cleanup, without cleaning swept children twice",
          result.cleanupOrder == std::vector<std::string>{"destroyed", "child", "app"});
    headless::active = false;
    internal::endFrameworkLifetime();
}

// A worker paused before the App slot holds the notification snapshot while
// the runner finishes. It must neither retain the App nor destroy it off-main.
struct SnapshotProbe {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
    int mainNotifications = 0;
    std::atomic<int> destructors{0};
    std::atomic<bool> destroyedOnMain{false};
    std::thread worker;
};
SnapshotProbe* snapshotProbe = nullptr;
struct SnapshotApp : App {
    ~SnapshotApp() override {
        ++snapshotProbe->destructors;
        snapshotProbe->destroyedOnMain = isMainThread();
    }
    void setup() override {
        snapshotProbe->worker = std::thread([] { fixtureEvents().update.notify(); });
        std::unique_lock<std::mutex> lock(snapshotProbe->mutex);
        check("worker acquired a headless update snapshot", snapshotProbe->cv.wait_for(
            lock, std::chrono::seconds(10), [] { return snapshotProbe->entered; }));
    }
    void update() override { exitApp(); }
};
void testWorkerSnapshot() {
    SnapshotProbe state;
    snapshotProbe = &state;
    auto blocker = fixtureEvents().update.listen([&] {
        if (isMainThread()) {
            ++state.mainNotifications;
            return;
        }
        std::unique_lock<std::mutex> lock(state.mutex);
        state.entered = true;
        state.cv.notify_all();
        state.cv.wait(lock, [&] { return state.release; });
    });
    check("worker-snapshot runner exits", runHeadlessApp<SnapshotApp>(HeadlessSettings().setFps(1024)) == 0);
    check("headless event delivery destroys App on main before returning despite worker snapshot",
          state.mainNotifications == 1 && state.destructors == 1 && state.destroyedOnMain);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.release = true;
    }
    state.cv.notify_all();
    state.worker.join();
    check("releasing a worker snapshot does not destroy App again or off-main",
          state.destructors == 1 && state.destroyedOnMain);
    snapshotProbe = nullptr;
}

struct TweenApp : App {
    Tween<float> tween{0, 1, 3.0f / 1024};
    int frames = 0;
    uint64_t initialCount = 0;
    void setup() override { initialCount = getUpdateCount(); tween.start(); }
    void update() override {
        check("Tween advances before the headless App update",
              std::abs(tween.getElapsed() - (++frames / 1024.0f)) < 0.000001f);
        check("headless shared update counter advances every step", getUpdateCount() == initialCount + frames);
        if (frames == 3) {
            check("headless Tween completes", tween.isComplete() && tween.getValue() == 1);
            exitApp();
        }
    }
};

#ifndef __EMSCRIPTEN__
// Queue the entire burst before updates so each step has deterministic work.
constexpr int udpStepBudget = 256;
constexpr int udpBurstSize = udpStepBudget + 17;
bool exitDuringUdpReceive = false;
int udpBurstExits = 0;
struct UdpBurstApp : App {
    UdpSocket receiver, sender;
    EventListener receive;
    int received = 0, frames = 0;
    bool ordered = true, onMain = true;
    void setup() override {
        receiver.setBindAddress("127.0.0.1");
        receiver.setUseThread(false);
        check("UDP burst receiver creates", receiver.create());
        check("UDP burst receive queue configured", receiver.setReceiveBufferSize(4 * 1024 * 1024));
        check("UDP burst receiver binds", receiver.bind(0));
        receive = receiver.onReceive.listen([this](UdpReceiveEventArgs& e) {
            ordered = ordered && std::string(e.data.begin(), e.data.end()) == std::to_string(received);
            onMain = onMain && std::this_thread::get_id() == mainThread;
            if (++received == 1 && exitDuringUdpReceive) exitApp();
        });
        bool sent = true;
        for (int i = 0; i < udpBurstSize; ++i) {
            sent = sender.sendTo("127.0.0.1", receiver.getLocalPort(), std::to_string(i)) && sent;
        }
        check("UDP burst queued before the first update", sent);
    }
    void update() override {
        if (++frames == 1) {
            check("one UDP update yields to App after exactly 256 datagrams", received == udpStepBudget);
        } else {
            check("later UDP update delivers all deferred datagrams", received == udpBurstSize);
            exitApp();
        }
    }
    void exit() override {
        ++udpBurstExits;
        check("UDP burst delivery stays ordered on main", ordered && onMain);
        check("UDP burst App exits after the expected update step", frames == (exitDuringUdpReceive ? 1 : 2));
        check("UDP receive exit request is honored with packets still deferred",
              received == (exitDuringUdpReceive ? udpStepBudget : udpBurstSize));
        receiver.close();
        sender.close();
    }
};
void testUdpBurst(bool requestExit) {
    exitDuringUdpReceive = requestExit;
    udpBurstExits = 0;
    check("UDP burst runner exits successfully", runHeadlessApp<UdpBurstApp>(HeadlessSettings().setFps(1024)) == 0);
    check("UDP burst App exit runs exactly once", udpBurstExits == 1);
}

struct NetworkApp : App {
    TcpServer server;
    TcpClient client;
    UdpSocket receiver, sender;
    EventListener accept, tcpReceive, udpReceive;
    std::string tcpData, udpData;
    bool tcpOnMain = false, udpOnMain = false;
    std::chrono::steady_clock::time_point deadline;
    void setup() override {
        accept = server.onClientConnect.listen([this](TcpClientConnectEventArgs& e) {
            server.send(e.clientId, "headless TCP");
        });
        tcpReceive = client.onReceive.listen([this](TcpReceiveEventArgs& e) {
            tcpData.append(e.data.begin(), e.data.end());
            tcpOnMain = std::this_thread::get_id() == mainThread;
        });
        udpReceive = receiver.onReceive.listen([this](UdpReceiveEventArgs& e) {
            udpData.assign(e.data.begin(), e.data.end());
            udpOnMain = std::this_thread::get_id() == mainThread;
        });
        client.setUseThread(false);
        receiver.setBindAddress("127.0.0.1");
        receiver.setUseThread(false);
        server.setBindAddress("127.0.0.1");
        const bool listening = server.start(0);
        check("loopback TCP server starts", listening);
        check("update-mode TCP client connects", listening && client.connect("127.0.0.1", server.getPort()));
        check("update-mode UDP receiver binds", receiver.bind(0));
        check("loopback UDP datagram sent", sender.sendTo("127.0.0.1", receiver.getLocalPort(), "headless UDP"));
        // Only a failure deadline: success depends on receiving the payloads,
        // never on the number of frames or how quickly this machine runs.
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    }
    void update() override {
        if ((tcpData == "headless TCP" && udpData == "headless UDP")
                || std::chrono::steady_clock::now() >= deadline) exitApp();
    }
    void exit() override {
        check("TCP update-mode receive works headless on main thread", tcpData == "headless TCP" && tcpOnMain);
        check("UDP update-mode receive works headless on main thread", udpData == "headless UDP" && udpOnMain);
        client.disconnect(); server.stop(); receiver.close(); sender.close();
    }
};
#endif
} // namespace

TC_CORE_TEST_MAIN() {
    mainThread = std::this_thread::get_id();
    getMainThreadId();
    testInvalidFps();
    testCatchUp();
    testTreeLifecycle();
    testDelivery(Mode::Order);
    testDelivery(Mode::ClearBeforeApp);
    testDelivery(Mode::ClearInApp);
    testWorkerSnapshot();
    check("Tween runner exits successfully", runHeadlessApp<TweenApp>(HeadlessSettings().setFps(1024)) == 0);
#ifndef __EMSCRIPTEN__
    testUdpBurst(false);
    testUdpBurst(true);
    check("loopback runner exits successfully", runHeadlessApp<NetworkApp>(HeadlessSettings().setFps(1024)) == 0);
#endif
    std::printf("headlessEvents: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
