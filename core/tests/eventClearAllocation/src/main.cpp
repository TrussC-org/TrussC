#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>
#include <thread>
#include "tc/network/tcNetworkState.h"

namespace {
thread_local bool observe = false;
thread_local std::size_t allocationLimit = std::numeric_limits<std::size_t>::max();
thread_local std::size_t allocationCount = 0, largestAllocation = 0;
}
// An owned binary keeps these allocation hooks out of the combined runner.
void* operator new(std::size_t n) {
    if (observe) {
        ++allocationCount;
        if (n > largestAllocation) largestAllocation = n;
        if (n > allocationLimit) throw std::bad_alloc();
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
template<class T> void clearAllocation(bool keepFramework) {
    Event<T> event;
    EventListener framework;
    if (keepFramework) framework = internal::FrameworkEventAccess::listen(event, [](auto&...) {}, 100);
    std::vector<EventListener> listeners;
    for (int i = 0; i < 1024; ++i) listeners.push_back(event.listen([](auto&...) {}));
    allocationCount = largestAllocation = 0;
    // Neither empty storage nor protected survivors may allocate, including
    // MSVC Debug's allocating default-container iterator proxies.
    allocationLimit = 0;
    bool threw = false;
    observe = true;
    try { event.clear(); } catch (const std::bad_alloc&) { threw = true; }
    observe = false;
    const bool ok = !threw && event.listenerCount() == (keepFramework ? 1u : 0u) &&
        allocationCount == 0;
    std::printf("Event<%s>: clear 1024 ordinary, %d framework, limit=%zu: %s (allocations=%zu, largest=%zu)\n",
                std::is_void_v<T> ? "void" : "int", int(keepFramework), allocationLimit,
                ok ? "PASS" : "FAIL", allocationCount, largestAllocation);
    if (!ok) ++failures;
}
template<class T> void mainStoreAllocation(bool snapshotPath, bool mainFirst) {
    using External = internal::ExternalEventDelivery<T>;
    using Delivery = internal::EventSnapshotDelivery<T>;
    Event<T> source;
    int inlineCalls = 0, mainCalls = 0;
    auto mainListener = source.listen([&](auto&...) { ++mainCalls; }, Deliver::Main, mainFirst ? -1 : 1);
    auto inlineListener = source.listen([&](auto&...) { ++inlineCalls; }, 0);
    auto gate = std::make_shared<internal::NetworkDelivery>();
    auto target = Delivery::bind(source, gate);
    Delivery delivery{target};
    bool accepted = true, threw = false;
    size_t failedAllocations = 0;
    auto emit = [&] {
        if constexpr (std::is_void_v<T>) {
            if (snapshotPath) delivery.notifyExternal();
            else accepted = External::notify(source);
        } else {
            T value{};
            accepted = snapshotPath ? delivery.notifyExternal(value) : External::notify(source, value);
        }
    };
    std::thread worker([&] {
        allocationCount = 0;
        allocationLimit = 0;
        observe = true;
        try { emit(); } catch (const std::bad_alloc&) { threw = true; }
        observe = false;
        failedAllocations = allocationCount;
    });
    worker.join();
    internal::drainMainThreadQueue();
    bool ok = !threw && failedAllocations == 1 && inlineCalls == 1 && mainCalls == 0 &&
        !External::diagnostics(source);
    if constexpr (!std::is_void_v<T>) ok = ok && !accepted;
    else if (!snapshotPath) ok = ok && !accepted;
    // A later successful store acquisition preserves the earlier loss count.
    std::thread recovery([&] { emit(); });
    recovery.join();
    auto store = External::diagnostics(source);
    ok = ok && store && store->rejectedCopies == 1 && store->pending == 1;
    internal::drainMainThreadQueue();
    ok = ok && inlineCalls == 2 && mainCalls == 1 && store->pending == 0;
    std::printf("Event<%s>: failed lazy store, snapshot=%d, mainFirst=%d: %s\n",
        std::is_void_v<T> ? "void" : "int", int(snapshotPath), int(mainFirst), ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
} // namespace
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    for (bool snapshot : {false, true}) for (bool mainFirst : {false, true}) {
        mainStoreAllocation<int>(snapshot, mainFirst);
        mainStoreAllocation<void>(snapshot, mainFirst);
    }
    clearAllocation<int>(false); clearAllocation<void>(false);
    clearAllocation<int>(true); clearAllocation<void>(true);
    return failures ? 1 : 0;
}
