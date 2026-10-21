// #364: receive data survives app stalls; overflow keeps the newest bytes.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>

#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#endif

using namespace std;
using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

template<class F> bool until(F condition) {
    // Failure deadline only; successful checks depend on observed data/state.
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    do {
        if (condition()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    } while (chrono::steady_clock::now() < deadline);
    return false;
}

#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
struct Pty {
    int master = -1;
    string path;
    bool open() {
        master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (master < 0 || grantpt(master) || unlockpt(master)) return false;
        const char* name = ptsname(master);
        if (!name) return false;
        path = name;
        return true;
    }
    ~Pty() { if (master >= 0) ::close(master); }
    bool send(const vector<uint8_t>& data) {
        size_t offset = 0;
        auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
        do {
            const ssize_t n = ::write(master, data.data() + offset, data.size() - offset);
            if (n > 0) {
                offset += static_cast<size_t>(n);
                // Bound stalls, not throughput on a busy runner.
                deadline = chrono::steady_clock::now() + chrono::seconds(10);
            }
            if (offset == data.size()) return true;
            this_thread::sleep_for(chrono::milliseconds(1));
        } while (chrono::steady_clock::now() < deadline);
        return false;
    }
};
#endif
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
int* waitingBacklog = nullptr;
int* waitingLoss = nullptr;
struct WarningDeliveryApp : App {
    chrono::steady_clock::time_point deadline;
    void setup() override { deadline = chrono::steady_clock::now() + chrono::seconds(10); }
    void update() override {
        if ((*waitingBacklog == 1 && *waitingLoss == 1) || chrono::steady_clock::now() > deadline)
            requestExit();
    }
};
int appUpdates = 0;
void countAppUpdate() { ++appUpdates; }
#endif
// Successful allocations use malloc, just like the production allocator.
size_t allocationBudget = 0;
void* limitedAllocate(size_t bytes) {
    if (!allocationBudget) return nullptr;
    --allocationBudget;
    return std::malloc(bytes);
}

} // namespace

TC_CORE_TEST_MAIN() {
    constexpr size_t cap = 64 * 1024 * 1024;
    const size_t total = cap + 131071;
    vector<uint8_t> input(total);
    for (size_t i = 0; i < total; ++i) input[i] = static_cast<uint8_t>((i * 17 + i / 251) % 256);
    // Shared policy tests run on every platform, without a device or display.
    {
        internal::SerialRxBuffer rx;
        check("unused RX allocates no chunks", rx.allocatedBytes() == 0);
        rx.append(input.data(), 1024 * 1024);
        check("exactly 1 MiB does not warn", !rx.takeWarnings().backlog && rx.dropped == 0);
        rx.append(input.data(), 1);
        auto warning = rx.takeWarnings();
        check("above 1 MiB warns once without loss", warning.backlog && !warning.loss && rx.dropped == 0);
        rx.clear();
        rx.append(input.data(), 1024 * 1024 + 1);
        check("backlog warning does not repeat on refill", !rx.takeWarnings().backlog);
        rx.clear();
        check("clear releases all RX chunks", rx.allocatedBytes() == 0);
        rx.append(input.data(), cap);
        check("64 MiB fits without loss", rx.size() == cap && rx.dropped == 0);
        rx.append(input.data() + cap, total - cap);
        warning = rx.takeWarnings();
        check("cap plus k drops exactly k", rx.size() == cap && rx.dropped == total - cap && warning.loss);
        vector<uint8_t> newest(cap);
        check("overflow retains complete newest suffix and final byte", rx.read(newest.data(), cap) == static_cast<int>(cap) &&
              equal(newest.begin(), newest.end(), input.end() - cap));
        check("drain releases peak RX allocation", rx.size() == 0 && rx.allocatedBytes() == 0);
        rx.append(input.data(), 7);
        check("small refill allocates only one chunk", rx.allocatedBytes() > 0 && rx.allocatedBytes() < 32 * 1024);
        rx.clear();
        const size_t previous = rx.dropped.load();
        for (size_t flood = 1; flood <= 3; ++flood) {
            rx.append(input.data(), total);
            check("huge chunk and repeated floods stay bounded", rx.size() == cap &&
                  rx.allocatedBytes() < cap + 128 * 1024 &&
                  rx.dropped == previous + flood * total - cap);
            check("drop warning does not repeat", !rx.takeWarnings().loss);
        }
        check("single huge chunk keeps newest suffix", rx.read(newest.data(), cap) == static_cast<int>(cap) &&
              equal(newest.begin(), newest.end(), input.end() - cap));
        const size_t lost = rx.dropped.load();
        rx.clear();
        check("flush preserves count", rx.dropped == lost);
        rx.resetConnection();
        check("new connection resets counter and releases storage", rx.dropped == 0 && rx.allocatedBytes() == 0);
        rx.append(input.data(), total);
        warning = rx.takeWarnings();
        check("new connection resets both once gates", warning.backlog && warning.loss);
    }
    {
        internal::SerialRxBuffer rx;
        rx.append(input.data(), 1024 * 1024 + 1);
        rx.clear();
        auto warning = rx.takeWarnings();
        check("backlog snapshot survives flush before delivery", warning.backlog && warning.buffered == 1024 * 1024 + 1);
        rx.append(input.data(), total);
        rx.clear();
        warning = rx.takeWarnings();
        check("loss snapshot survives flush before delivery", warning.loss && !warning.backlog &&
              warning.dropped == total - cap && warning.needed == total);
    }
    {
        internal::SerialRxBuffer rx(limitedAllocate);
        allocationBudget = 1;
        rx.append(input.data(), internal::SerialRxBuffer::chunkSize + 7);
        auto warning = rx.takeWarnings();
        check("allocation failure counts only unadmitted bytes", rx.size() == internal::SerialRxBuffer::chunkSize &&
              rx.dropped == 7 && warning.loss && warning.allocationFailed);
        allocationBudget = 1;
        rx.append(input.data(), 1);
        check("allocation failure permits later input", rx.size() == internal::SerialRxBuffer::chunkSize + 1 && rx.dropped == 7);
        rx.clear();
        allocationBudget = 0;
        rx.append(input.data(), total);
        check("oversized chunk allocation failure counts every byte", rx.size() == 0 && rx.dropped == total + 7 && rx.allocatedBytes() == 0);
    }
    {
        // The loss warning reports what the failed append needed, not an
        // earlier, already consumed backlog.
        internal::SerialRxBuffer rx(limitedAllocate);
        allocationBudget = 1000;
        rx.append(input.data(), 32768);
        std::vector<uint8_t> sink(32768);
        rx.read(sink.data(), static_cast<int>(sink.size()));
        allocationBudget = 0;
        rx.append(input.data(), 1);
        auto warning = rx.takeWarnings();
        check("allocation-loss warning reports this append's need", warning.loss && warning.allocationFailed &&
              rx.dropped == 1 && warning.needed == 1);
    }
#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    printf("SKIP: serialRxBuffer integration needs a POSIX pseudo-terminal\n");
    return failures ? 1 : 0;
#else
    // Delivery uses the main-thread queue, not the update event.
    const size_t initialUpdateListeners = events().update.listenerCount();
    Pty pty;
    Serial serial;
    check("new Serial counter is zero", serial.getDroppedByteCount() == 0);
    if (!pty.open() || !serial.setup(pty.path, 115200)) {
        check("pty setup", false);
        return 1;
    }
    check("connection registers no update listener", events().update.listenerCount() == initialUpdateListeners);
    int overflowWarnings = 0;
    bool warningHasTotal = false;
    int backlogWarnings = 0;
    const auto appThread = this_thread::get_id();
    bool warningsOnAppThread = true;
    EventListener warnings = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.message.find("Serial: RX buffer exceeds 1 MiB") != string::npos) {
            ++backlogWarnings;
            warningsOnAppThread &= this_thread::get_id() == appThread;
        }
        if (event.message.find("Serial: RX buffer overflow") != string::npos) {
            ++overflowWarnings;
            warningsOnAppThread &= this_thread::get_id() == appThread;
            warningHasTotal = event.message.find("total dropped bytes:") != string::npos &&
                              event.message.find("capacity bytes: 67108864") != string::npos;
            // Inline log callbacks must run with the Serial's locks released.
            serial.available();
            serial.getDroppedByteCount();
        }
    });
    // No Serial reads or available() calls while the worker drains > 64 MiB.
    check("device writes the complete stream while app does not read", pty.send(input));
    check("flood queues exactly one delivery for the connection", until([&] {
        return serial.getDroppedByteCount() == total - cap;
    }) && internal::countQueuedMainThreadSlots() == 1);
    // Move the live connection before delivery; run the real framework loop.
    // The headless runner drains the main-thread queue; it fires no update event.
    Serial pendingMove(std::move(serial));
    serial = std::move(pendingMove);
    waitingBacklog = &backlogWarnings;
    waitingLoss = &overflowWarnings;
    runHeadlessApp<WarningDeliveryApp>(HeadlessSettings().setFps(1000));
    check("first-drop warning arrives in headless loop without another Serial poll",
          overflowWarnings == 1 && backlogWarnings == 1);
    check("headless delivery leaves nothing queued", internal::countQueuedMainThreadSlots() == 0);
    const bool received = until([&] { return serial.getDroppedByteCount() == total - cap; });
    check("exact overflow count", received);
    check("available counts the capped TrussC buffer", serial.available() == static_cast<int>(cap));
    check("overflow warning includes total and permits inline callbacks", overflowWarnings == 1 && warningHasTotal && warningsOnAppThread);
    Serial moved(std::move(serial));
    check("move carries buffer and counter", !serial.isConnected() && moved.getDroppedByteCount() == total - cap);
    serial = std::move(moved);
    vector<uint8_t> output(cap);
    check("newest bytes survive a stall and both moves",
          serial.readBytes(output.data(), output.size()) == static_cast<int>(cap) &&
          equal(output.begin(), output.end(), input.end() - cap));
    check("reading does not count intentional consumption", serial.available() == 0 && serial.getDroppedByteCount() == total - cap);
    vector<uint8_t> small(input.begin(), input.begin() + 4096);
    check("device sends data below cap", pty.send(small));
    check("worker receives below-cap data without an app read", until([&] { return serial.available() == 4096; }));
    serial.flushInput();
    check("flushInput clears buffer and preserves counter", serial.available() == 0 && serial.getDroppedByteCount() == total - cap);
    check("data arrives after flushInput", pty.send(small) && until([&] { return serial.available() == 4096; }));
    serial.flush();
    check("flush clears buffer and preserves counter", serial.available() == 0 && serial.getDroppedByteCount() == total - cap);
    serial.close();
    check("close preserves counter", !serial.isConnected() && serial.getDroppedByteCount() == total - cap);
    internal::drainMainThreadQueue();
    check("gated warnings do not repeat after close", backlogWarnings == 1 && overflowWarnings == 1);
    Pty next;
    check("new connection resets counter", next.open() && serial.setup(next.path, 9600) && serial.getDroppedByteCount() == 0);
    check("below-cap stall loses no bytes", next.send(small) && until([&] { return serial.available() == 4096; }));
    vector<uint8_t> fresh(4096);
    check("below-cap contents survive", serial.readBytes(fresh.data(), fresh.size()) == 4096 && fresh == small && serial.getDroppedByteCount() == 0);
    vector<uint8_t> backlog(input.begin(), input.begin() + 1024 * 1024 + 1);
    // EVENT_DRIVEN with no redraw: the frame drains the queue, update never runs.
    {
        check("reopened connection queues a fresh backlog delivery", next.send(backlog) &&
              until([&] { return internal::countQueuedMainThreadSlots() == 1; }));
        int updateEvents = 0;
        EventListener updateCounter = events().update.listen([&] { ++updateEvents; });
        auto* previousUpdate = internal::appUpdateFunc;
        internal::appUpdateFunc = countAppUpdate;
        setFps(EVENT_DRIVEN);
        internal::mainLoop().redrawCount = 0;
        const bool drew = internal::beginMainLoopFrame(chrono::steady_clock::now());
        if (drew) internal::runSyncedUpdate();
        check("idle EVENT_DRIVEN frame delivers the warning without update",
              !drew && backlogWarnings == 2 && appUpdates == 0 && updateEvents == 0);
        internal::appUpdateFunc = previousUpdate;
        setFps(VSYNC);
    }
    // A connection closed and reopened before the drain: the old delivery
    // stays valid and reports the old connection; the new one starts fresh.
    {
        Pty first, second;
        Serial reopened;
        check("reopen-before-drain setup", first.open() && reopened.setup(first.path, 115200));
        check("old connection queues its warning", first.send(backlog) &&
              until([&] { return internal::countQueuedMainThreadSlots() == 1; }));
        reopened.close();
        check("reopen before drain", second.open() && reopened.setup(second.path, 115200) &&
              reopened.getDroppedByteCount() == 0);
        internal::drainMainThreadQueue();
        check("queued warning of the closed connection is delivered once", backlogWarnings == 3 &&
              internal::countQueuedMainThreadSlots() == 0);
        check("reopened connection still gets its own backlog warning", second.send(backlog) && until([&] {
            internal::drainMainThreadQueue();
            return backlogWarnings == 4;
        }));
    }
    serial.close();
    // A connection destroyed while its delivery is queued.
    {
        Pty gonePty;
        auto gone = make_unique<Serial>();
        check("destroy-before-drain setup", gonePty.open() && gone->setup(gonePty.path, 115200));
        check("destroyed connection had a queued delivery", gonePty.send(backlog) &&
              until([&] { return internal::countQueuedMainThreadSlots() == 1; }));
        gone.reset();
        internal::drainMainThreadQueue();
        check("delivery after destruction is safe and reports once", backlogWarnings == 5 &&
              internal::countQueuedMainThreadSlots() == 0);
    }
    // A log listener can destroy the whole connection during delivery.
    // The drain holds the RX state until the delivery returns.
    Pty destroyPty;
    auto dying = make_unique<Serial>();
    check("reentrant-destruction setup", destroyPty.open() && dying->setup(destroyPty.path, 115200));
    auto destroyListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message.find("Serial: RX buffer exceeds 1 MiB") != string::npos) dying.reset();
    });
    check("warning callback can destroy connection", destroyPty.send(backlog) && until([&] {
        internal::drainMainThreadQueue();
        return !dying;
    }));
    check("reentrant destruction leaves nothing queued", internal::countQueuedMainThreadSlots() == 0);
    destroyListener.disconnect();
    // Shared connection state, driven directly as a worker would.
    {
        auto state = make_shared<internal::SerialRxState>();
        weak_ptr<internal::SerialRxState> weak = state;
        const int backlogBefore = backlogWarnings, overflowBefore = overflowWarnings;
        size_t maxQueued = 0;
        {
            lock_guard<mutex> lock(state->buffer.mutex);
            for (size_t offset = 0; offset < total; offset += 4096) {
                state->append(input.data() + offset, min<size_t>(4096, total - offset));
                maxQueued = max(maxQueued, internal::countQueuedMainThreadSlots());
            }
        }
        check("flood of appends queues at most one delivery", maxQueued == 1 && state->isQueued() &&
              state->buffer.dropped == total - cap);
        state.reset();
        check("queued delivery keeps destroyed connection state alive", !weak.expired());
        internal::drainMainThreadQueue();
        check("one delivery carries both warnings", backlogWarnings == backlogBefore + 1 &&
              overflowWarnings == overflowBefore + 1);
        check("drain releases the destroyed connection state", weak.expired() &&
              internal::countQueuedMainThreadSlots() == 0);
    }
    {
        auto state = make_shared<internal::SerialRxState>();
        {
            lock_guard<mutex> lock(state->buffer.mutex);
            state->append(input.data(), total);
        }
        internal::drainMainThreadQueue();
        {
            lock_guard<mutex> lock(state->buffer.mutex);
            for (int flood = 0; flood < 3; ++flood) state->append(input.data(), total);
        }
        check("repeated floods after delivery queue nothing", !state->isQueued() &&
              internal::countQueuedMainThreadSlots() == 0);
    }
    check("Serial leaves the update listeners unchanged", events().update.listenerCount() == initialUpdateListeners);
    return failures ? 1 : 0;
#endif
}
