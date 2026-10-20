#pragma once

// Test-only C++ allocation trace. Library-internal C/TLS allocations are not
// covered; the report keeps that backend-verification limitation explicit.
#ifdef __linux__
#include <atomic>
#include <cstdlib>
#include <new>
#include <cstddef>
namespace responseMemoryTrace {
std::atomic<bool> enabled{false};
std::atomic<size_t> live{0}, peak{0};
struct alignas(std::max_align_t) Header { size_t bytes; bool counted; };
void* allocate(size_t bytes) {
    if (bytes > SIZE_MAX - sizeof(Header)) throw std::bad_alloc();
    auto* header = static_cast<Header*>(std::malloc(bytes + sizeof(Header)));
    if (!header) throw std::bad_alloc();
    header->bytes = bytes;
    header->counted = enabled.load();
    if (header->counted) {
        const size_t amount = live.fetch_add(bytes) + bytes;
        size_t previous = peak.load();
        while (amount > previous && !peak.compare_exchange_weak(previous, amount)) {}
    }
    return header + 1;
}
void release(void* pointer) noexcept {
    if (!pointer) return;
    auto* header = static_cast<Header*>(pointer) - 1;
    if (header->counted) live.fetch_sub(header->bytes);
    std::free(header);
}
void begin() { peak.store(live.load()); enabled.store(true); }
void end() { enabled.store(false); }
}
void* operator new(size_t bytes) { return responseMemoryTrace::allocate(bytes); }
void* operator new[](size_t bytes) { return responseMemoryTrace::allocate(bytes); }
void operator delete(void* pointer) noexcept { responseMemoryTrace::release(pointer); }
void operator delete[](void* pointer) noexcept { responseMemoryTrace::release(pointer); }
void operator delete(void* pointer, size_t) noexcept { responseMemoryTrace::release(pointer); }
void operator delete[](void* pointer, size_t) noexcept { responseMemoryTrace::release(pointer); }
#endif
