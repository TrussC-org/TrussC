// Device lifecycle (#415) for MidiIn and MidiOut, driven through the libremidi
// double: the "OS" removes a port (test::unplugged + the observer's removal
// callback, fired from a worker thread as a backend would). Synchronous, no sleeps.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiIn.h"
#include "tcxMidiOut.h"

#include <cstdio>
#include <memory>
#include <thread>

namespace {
using namespace tcx::midi;
int checks = 0;
int failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

template <class Dev> struct Kind;
template <> struct Kind<MidiIn> {
    static constexpr const char* name = "MidiIn";
    static constexpr const char* tag = "[tcxMidiIn]";
    static constexpr const char* hardware = "test input";
    static constexpr const char* software = "virtual source";
    static void remove(const std::string& port) { libremidi::test::removeInput(port); }
};
template <> struct Kind<MidiOut> {
    static constexpr const char* name = "MidiOut";
    static constexpr const char* tag = "[tcxMidiOut]";
    static constexpr const char* hardware = "test output";
    static constexpr const char* software = "virtual destination";
    static void remove(const std::string& port) { libremidi::test::removeOutput(port); }
};

// The backend's thread reports a removal (the port is already gone from the lists).
template <class Dev> void backendRemoves(const std::string& port, int times = 1) {
    std::thread backend([&] { for (int i = 0; i < times; ++i) Kind<Dev>::remove(port); });
    backend.join();
}

template <class Dev> void lifecycle() {
    const std::string label = Kind<Dev>::name;
    const std::string hw = Kind<Dev>::hardware;
    const std::string sw = Kind<Dev>::software;
    const auto mainThread = std::this_thread::get_id();
    // Other tests' ports share the double, so a removal would reach them too.
    check(libremidi::test::observersCreated == libremidi::test::observersDestroyed,
          label + ": starts with no live watch");
    auto& test = libremidi::test::unplugged;
    test.clear();
    libremidi::test::virtualPorts = true;
    tc::internal::drainMainThreadQueue();

    struct Line { tc::LogLevel level; std::string message; };
    std::vector<Line> logs;
    auto logListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
        if (e.message.find(Kind<Dev>::tag) != std::string::npos) logs.push_back({e.level, e.message});
    });
    auto count = [&](tc::LogLevel level) {
        size_t n = 0;
        for (const auto& l : logs) n += l.level == level;
        return n;
    };

    Dev dev;
    int fired = 0;
    std::string reason;
    bool connectedWhenFired = true, firedOnMain = true;
    std::function<void()> inListener;
    auto listener = dev.onDisconnect.listen([&](tc::DeviceDisconnectEventArgs& e) {
        ++fired;
        reason = e.reason;
        connectedWhenFired = dev.isConnected();
        firedOnMain = std::this_thread::get_id() == mainThread;
        if (inListener) inListener();
    });

    // Rule 1: isConnected() is true only while the port is open.
    check(!dev.isConnected() && !dev.isOpen(), label + ": not connected before any open");
    check(dev.openPort(hw) && dev.isConnected() && dev.isOpen(), label + ": connected after open");
    dev.close();
    check(!dev.isConnected() && !dev.isOpen() && dev.getName().empty(), label + ": not connected after close");
    check(dev.openPort(hw), label + ": reopen");
    dev.closePort();
    check(!dev.isConnected(), label + ": closePort is an alias of close");

    // Rule 2: setup() reopens the last port, by name.
    Dev fresh;
    const size_t errorsBefore = count(tc::LogLevel::Error);
    check(!fresh.setup() && !fresh.isConnected() && count(tc::LogLevel::Error) == errorsBefore + 1,
          label + ": setup() without a previous port fails with one error");
    check(dev.setup() && dev.isConnected() && dev.getName() == hw, label + ": setup() reopens the last port");
    check(dev.setup() && dev.isConnected(), label + ": setup() on an open port reopens it");
    dev.close();
    check(dev.openPort(sw) && dev.getPort() == 1, label + ": software port is the second one");
    dev.close();
    test = {hw};  // the hardware port is gone: indices shift, the name does not
    check(dev.setup() && dev.getName() == sw && dev.getPort() == 0, label + ": setup() finds the port by name when indices shift");
    dev.close();
    test = {sw};
    const size_t errorsGone = count(tc::LogLevel::Error);
    check(!dev.setup() && !dev.isConnected() && count(tc::LogLevel::Error) == errorsGone + 1,
          label + ": setup() fails with one error when the port is gone");
    test.clear();
    check(!dev.openPort("missing port") && dev.setup() && dev.getName() == sw,
          label + ": a failed open does not change the last port");
    dev.close();
    check(dev.openVirtualPort("virtual one"), label + ": open virtual port");
    dev.close();
    check(dev.setup() && dev.isVirtual() && dev.getName() == "virtual one", label + ": setup() reopens the virtual port");
    dev.close();

    // Rule 3: close() and opening another port never fire onDisconnect.
    check(dev.openPort(hw), label + ": open before close checks");
    check(dev.openPort(sw) && dev.setup(), label + ": opening over an open port");
    dev.close();
    dev.close();
    tc::internal::drainMainThreadQueue();
    check(fired == 0, label + ": close, reopen and setup never fire onDisconnect");

    // Rule 4: a removal fires once, on the main thread, with isConnected() false.
    check(dev.openPort(hw), label + ": open for loss");
    const int created = libremidi::test::observersCreated;
    test = {hw};
    backendRemoves<Dev>(hw);
    check(fired == 0, label + ": nothing runs on the backend's thread");
    const size_t warningsBefore = count(tc::LogLevel::Warning);
    tc::internal::drainMainThreadQueue();
    check(fired == 1 && firedOnMain, label + ": fires once, on the main thread");
    check(!connectedWhenFired && !dev.isConnected(), label + ": isConnected() is already false when it fires");
    check(reason.find(hw) != std::string::npos, label + ": reason names the port");
    check(count(tc::LogLevel::Warning) == warningsBefore + 1, label + ": one warning");
    check(libremidi::test::observersDestroyed == libremidi::test::observersCreated, label + ": the watch is gone after the loss");
    backendRemoves<Dev>(hw, 3);
    tc::internal::drainMainThreadQueue();
    check(fired == 1, label + ": a closed port does not fire again");

    // A removal storm is one event.
    test.clear();
    check(dev.setup() && dev.isConnected(), label + ": setup() after the device is back");
    test = {hw};
    backendRemoves<Dev>(hw, 5);
    backendRemoves<Dev>(sw, 2);
    tc::internal::drainMainThreadQueue();
    check(fired == 2 && !dev.isConnected(), label + ": several removals before the turn fire once");

    // A removal of another port is not our loss.
    test.clear();
    check(dev.setup(), label + ": reopen for unrelated removal");
    test = {sw};
    backendRemoves<Dev>(sw);
    tc::internal::drainMainThreadQueue();
    check(fired == 2 && dev.isConnected(), label + ": another port's removal leaves the port open");

    // A listener may reconnect with setup().
    int listenerCalls = 0;
    inListener = [&] {
        ++listenerCalls;
        test.clear();                   // the device came back
        check(dev.setup(), label + ": setup() from inside onDisconnect");
    };
    test = {hw};
    backendRemoves<Dev>(hw);
    tc::internal::drainMainThreadQueue();
    check(fired == 3 && listenerCalls == 1 && dev.isConnected(), label + ": a listener reconnects and the new port stays open");
    tc::internal::drainMainThreadQueue();
    check(fired == 3, label + ": the reconnect is not reported as a loss");
    inListener = {};
    backendRemoves<Dev>("something else");
    tc::internal::drainMainThreadQueue();
    check(fired == 3 && dev.isConnected(), label + ": the reopened port is watched, not lost");

    // A virtual port is ours, not the OS's to take away.
    dev.close();
    const int watching = libremidi::test::observersCreated;
    check(dev.openVirtualPort("virtual two"), label + ": open virtual");
    check(libremidi::test::observersCreated == watching, label + ": a virtual port has no watch");
    test = {sw};
    backendRemoves<Dev>(sw);
    tc::internal::drainMainThreadQueue();
    check(fired == 3 && dev.isConnected(), label + ": virtual port stays open");
    dev.close();
    test.clear();
    check(created >= 1, label + ": the opens created watches");
    check(libremidi::test::observersDestroyed == libremidi::test::observersCreated, label + ": no watch outlives its port");

    // Rule 5: nothing fires from the destructor, also with a delivery pending.
    int ghostFired = 0;
    {
        auto ghost = std::make_unique<Dev>();
        auto ghostListener = ghost->onDisconnect.listen([&](tc::DeviceDisconnectEventArgs&) { ++ghostFired; });
        check(ghost->openPort(hw), label + ": open before destruction");
        ghost.reset();
        tc::internal::drainMainThreadQueue();
        check(ghostFired == 0, label + ": destroying an open port does not fire");

        ghost = std::make_unique<Dev>();
        ghostListener = ghost->onDisconnect.listen([&](tc::DeviceDisconnectEventArgs&) { ++ghostFired; });
        check(ghost->openPort(hw), label + ": open before destruction with a delivery pending");
        test = {hw};
        backendRemoves<Dev>(hw);
        ghost.reset();
        tc::internal::drainMainThreadQueue();
        check(ghostFired == 0, label + ": a delivery queued before destruction does nothing");
    }
    test.clear();
    libremidi::test::virtualPorts = false;
    check(libremidi::test::observersDestroyed == libremidi::test::observersCreated, label + ": every watch destroyed at the end");
}
}

int testDeviceLifecycle() {
    tc::getMainThreadId();
#if !defined(__EMSCRIPTEN__)
    lifecycle<MidiIn>();
    lifecycle<MidiOut>();
#endif
    std::printf("device lifecycle: %d checks, %d failures\n", checks, failures);
    return failures;
}
