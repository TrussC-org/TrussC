// #947: real headless event delivery (based on the fc15145e fixture).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdio>
#include <string>
#include <thread>

using namespace tc;

namespace {
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
        listeners.push_back(events().update.listen([] { order += 'c'; }, EventPriority::App));
        listeners.push_back(events().update.listen([] { order += 'z'; }, EventPriority::AfterApp));
        listeners.push_back(events().exit.listen([] { order += 'C'; }, EventPriority::App));
        listeners.push_back(events().exit.listen([] { order += 'Z'; }, EventPriority::AfterApp));
        // Exercise the same protected, self-disconnecting ownership as mandatory
        // recorder/ImGui/inspector teardown. A live inspector is tested in its addon.
        listeners.push_back(internal::FrameworkEventAccess::listen(events().exit, [this] {
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
        if (mode == Mode::ClearInApp) events().exit.clear();
    }
    void cleanup() override { order += 'K'; ++cleanups; }
};

void testDelivery(Mode requested) {
    mode = requested;
    order.clear();
    updates = exits = cleanups = teardowns = childUpdates = modUpdates = timerCalls = 0;
    const auto updateCount = events().update.listenerCount();
    const auto exitCount = events().exit.listenerCount();
    auto defaultUpdate = events().update.listen([] {
        order += 'd';
        if (mode == Mode::ClearBeforeApp) events().update.clear();
    });
    auto beforeUpdate = events().update.listen([] { order += 'b'; }, EventPriority::App);
    auto defaultExit = events().exit.listen([] {
        order += 'D';
        if (mode == Mode::ClearBeforeApp) events().exit.clear();
    });
    auto beforeExit = events().exit.listen([] { order += 'B'; }, EventPriority::App);
    check("runHeadlessApp returns exit code", runHeadlessApp<EventsApp>(HeadlessSettings().setFps(1024)) == 7);
    check("App update, exit and cleanup survive clear and run exactly once per delivery",
          updates == 3 && exits == 1 && cleanups == 1 && teardowns == 1);
    const std::string expected = mode == Mode::Order ? "dbaczdbaczdbaczDBACZTK"
        : mode == Mode::ClearBeforeApp ? "daaaDATK" : "dbaczdbaczdbaczDBATK";
    std::printf("order: %s\n", order.c_str());
    check("default, tied 100, App, tied 100, AfterApp order / clear during notify", order == expected);
    check("headless still does not update children, mods or timers",
          childUpdates == 0 && modUpdates == 0 && timerCalls == 0);
    defaultUpdate.disconnect(); beforeUpdate.disconnect();
    defaultExit.disconnect(); beforeExit.disconnect();
    check("runner disconnects App bindings after returning",
          events().update.listenerCount() == updateCount && events().exit.listenerCount() == exitCount);
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
        snapshotProbe->worker = std::thread([] { events().update.notify(); });
        std::unique_lock<std::mutex> lock(snapshotProbe->mutex);
        check("worker acquired a headless update snapshot", snapshotProbe->cv.wait_for(
            lock, std::chrono::seconds(10), [] { return snapshotProbe->entered; }));
    }
    void update() override { exitApp(); }
};
void testWorkerSnapshot() {
    SnapshotProbe state;
    snapshotProbe = &state;
    auto blocker = events().update.listen([&] {
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
            if (++received == 1 && exitDuringUdpReceive) requestExitApp();
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
    // One update per pass: a late wake would otherwise owe catch-up steps, and
    // the runner applies a cancellable exit request only after all of them.
    const int savedSteps = getMaxUpdateSteps();
    setMaxUpdateSteps(1);
    check("UDP burst runner exits successfully", runHeadlessApp<UdpBurstApp>(HeadlessSettings().setFps(1024)) == 0);
    setMaxUpdateSteps(savedSteps);
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
