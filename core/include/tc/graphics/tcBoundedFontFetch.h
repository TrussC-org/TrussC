#pragma once

#ifdef __EMSCRIPTEN__
#include <cstddef>

namespace trussc::internal {
// Bound (security): remote fonts can expand or omit their length. Overflow rejects the whole fetch before completed-response buffering and is counted.
constexpr size_t fontFetchByteLimit = 64 * 1024 * 1024;
struct BoundedFontFetch {
    bool completing = false, closePending = false;
    void* userData = nullptr;
    const char* data = nullptr;
    size_t numBytes = 0;
    int status = 0;
    void (*complete)(BoundedFontFetch*, bool) = nullptr;
};
BoundedFontFetch* startBoundedFontFetch(const char* url, void* userData,
    unsigned timeout, void (*complete)(BoundedFontFetch*, bool));
void closeBoundedFontFetch(BoundedFontFetch* fetch);
size_t fontFetchLossCountForTests();
} // namespace trussc::internal
#endif
