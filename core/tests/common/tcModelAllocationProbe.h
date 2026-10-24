#pragma once

#include "tcTestFailure.h"

#include <cstdlib>
#include <new>
#include <cstddef>

namespace modelAllocationProbe {
inline bool active = false;
inline size_t largest = 0;
inline size_t failAt = 0;
inline bool failed = false;
inline bool persistent = false;
inline size_t requests = 0;
inline size_t totalBytes = 0;
inline size_t failRequest = 0;
inline bool observe(size_t bytes) {
    if (!active) return true;
    ++requests;
    totalBytes += bytes;
    if (bytes > largest) largest = bytes;
    if ((failed && persistent) || (!failed &&
        ((failAt && bytes >= failAt) || (failRequest && requests == failRequest)))) {
        failed = true;
        return false;
    }
    return true;
}
inline void begin(size_t failureSize = 0, bool persistentFailure = false) {
    largest = 0;
    failAt = failureSize;
    failed = false;
    persistent = persistentFailure;
    requests = totalBytes = failRequest = 0;
    active = true;
}
inline void beginNth(size_t request, bool persistentFailure) {
    begin(0, persistentFailure);
    failRequest = request;
}
inline void end() { active = false; }
}

// Instrument actual STL requests, including allocations in the addon library.
void* operator new(std::size_t bytes) {
    if (!modelAllocationProbe::observe(bytes)) throw std::bad_alloc();
    if (void* data = std::malloc(bytes ? bytes : 1)) return data;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* data) noexcept { std::free(data); }
void operator delete[](void* data) noexcept { std::free(data); }
void operator delete(void* data, std::size_t) noexcept { std::free(data); }
void operator delete[](void* data, std::size_t) noexcept { std::free(data); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    if (!modelAllocationProbe::observe(bytes)) return nullptr;
    return std::malloc(bytes ? bytes : 1);
}
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept {
    return ::operator new(bytes, tag);
}
void operator delete(void* data, const std::nothrow_t&) noexcept { std::free(data); }
void operator delete[](void* data, const std::nothrow_t&) noexcept { std::free(data); }
