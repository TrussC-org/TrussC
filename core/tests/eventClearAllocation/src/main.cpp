#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

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
    // With no survivors, even a one-byte allocation must fail. With one,
    // allow fixed-size storage but reject storage for the discarded 1024.
    allocationLimit = keepFramework ? 4096 : 0;
    bool threw = false;
    observe = true;
    try { event.clear(); } catch (const std::bad_alloc&) { threw = true; }
    observe = false;
    const bool ok = !threw && event.listenerCount() == (keepFramework ? 1u : 0u) &&
        (keepFramework ? largestAllocation <= 4096 : allocationCount == 0);
    std::printf("Event<%s>: clear 1024 ordinary, %d framework, limit=%zu: %s (allocations=%zu, largest=%zu)\n",
                std::is_void_v<T> ? "void" : "int", int(keepFramework), allocationLimit,
                ok ? "PASS" : "FAIL", allocationCount, largestAllocation);
    if (!ok) ++failures;
}
} // namespace
TC_CORE_TEST_MAIN() {
    clearAllocation<int>(false); clearAllocation<void>(false);
    clearAllocation<int>(true); clearAllocation<void>(true);
    return failures ? 1 : 0;
}
