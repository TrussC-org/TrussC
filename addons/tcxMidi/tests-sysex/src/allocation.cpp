// Thread-local allocation fault injection: enable only around the loss callback.
#include "allocation.h"
#include <cstdlib>
#include <new>
namespace allocation_test {
thread_local bool measure = false;
thread_local bool fail = false;
thread_local std::size_t allocations = 0;
}
void* operator new(std::size_t size) {
    if (allocation_test::measure) ++allocation_test::allocations;
    if (allocation_test::fail) throw std::bad_alloc();
    if (void* ptr = std::malloc(size ? size : 1)) return ptr;
    throw std::bad_alloc();
}
// Keep the deallocation boundary visible to GCC's mismatched-new-delete check.
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void operator delete(void* ptr) noexcept { std::free(ptr); }
TEST_NOINLINE void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
