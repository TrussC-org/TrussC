#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>

namespace {
thread_local bool failAll = false;
thread_local std::size_t failedAllocations = 0;
}
// An owned binary keeps these allocation hooks out of the combined runner.
void* operator new(std::size_t n) {
    if (failAll) { ++failedAllocations; throw std::bad_alloc(); }
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
void check(bool ok, const char* what) {
    std::printf("%s: %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
} // namespace
TC_CORE_TEST_MAIN() {
    // Reaching std::terminate is the failure this test guards against.
    std::set_terminate([] { std::fputs("FAIL: std::terminate reached\n", stdout); std::fflush(stdout); std::_Exit(1); });
    std::string seen;
    auto keep = getLogger().onLog.listen([&](LogEventArgs& e) { if (!failAll) seen = e.message; });

    logWarning("LogTest") << "ordinary " << 42;
    check(seen == "[LogTest] ordinary 42", "ordinary message reaches onLog unchanged");

    // Every allocation fails while the message is built and delivered.
    {
        LogStream stream = logWarning("LogTest");
        stream << "persistent exhaustion";
        failedAllocations = 0;
        failAll = true;
    }  // destructor runs here
    failAll = false;
    check(failedAllocations > 0, "destructor attempted allocation and survived its failure");

    // A listener that throws must not escape the destructor either.
    {
        auto thrower = getLogger().onLog.listen([](LogEventArgs&) { throw std::runtime_error("listener"); });
        logWarning("LogTest") << "throwing listener";
    }
    check(true, "throwing listener does not terminate");

    logNotice("LogTest") << "after";
    check(seen == "[LogTest] after", "logging works after failures");
    return failures ? 1 : 0;
}
