#include "tc/utils/tcThreadChannel.h"
#include "../../common/tcCoreTest.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
bool check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}
} // namespace

TC_CORE_TEST_MAIN() {
    bool ok = true;
    trussc::ThreadChannel<int> channel;
    const auto& state = channel;
    ok &= check("new channel is open", !state.isClosed());
    channel.close();
    channel.close();
    ok &= check("close is permanent and idempotent", state.isClosed());

    // The start gate precedes close(), so it does not synchronize the closed
    // flag's write with the concurrent reads. Run under ThreadSanitizer too:
    // observable values alone cannot prove the absence of a data race.
    bool monotonic = true;
    bool closedAfterJoin = true;
    for (int round = 0; round < 8; ++round) {
        trussc::ThreadChannel<int> shared;
        const auto& observed = shared;
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        std::array<bool, 4> stayedClosed{};
        std::array<std::thread, 4> readers;
        for (std::size_t i = 0; i < readers.size(); ++i) {
            readers[i] = std::thread([&, i] {
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                bool seenClosed = false;
                bool stayed = true;
                for (int read = 0; read < 10000; ++read) {
                    const bool closed = observed.isClosed();
                    stayed &= !seenClosed || closed;
                    seenClosed |= closed;
                }
                stayedClosed[i] = stayed;
            });
        }
        while (ready.load(std::memory_order_acquire) != 4) std::this_thread::yield();
        start.store(true, std::memory_order_release);
        shared.close();
        for (auto& reader : readers) reader.join();
        for (bool stayed : stayedClosed) monotonic &= stayed;
        closedAfterJoin &= observed.isClosed();
    }
    ok &= check("concurrent readers never observe a closed channel reopening", monotonic);
    ok &= check("every channel is closed after concurrent reads finish", closedAfterJoin);
    return ok ? 0 : 1;
}
