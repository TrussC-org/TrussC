#pragma once
#include <cstddef>
#include <cstdint>

namespace trussc::internal {
// Internal per-glyph workspace, independent of app-produced atlas pages.
class FontScratchScope {
public:
    explicit FontScratchScope(size_t sourceBytes, size_t pixelBytes = 0);
    ~FontScratchScope();
    FontScratchScope(const FontScratchScope&) = delete;
    FontScratchScope& operator=(const FontScratchScope&) = delete;
    bool failed() const { return failed_; }
private:
    friend void* fontScratchMalloc(size_t);
    friend void fontScratchFree(void*);
    friend void fontScratchFailure();
    friend size_t fontScratchRemaining();
    friend size_t fontScratchSourceBytes();
    FontScratchScope* previous_;
    size_t limit_;
    size_t sourceBytes_;
    size_t used_ = 0;
    bool failed_ = false;
};
void* fontScratchMalloc(size_t bytes);
void fontScratchFree(void* pointer);
void fontScratchFailure();
bool fontScratchFailed();
size_t fontScratchRemaining();
size_t fontScratchSourceBytes();
// Deterministic, thread-local allocation instrumentation (no public API).
void failFontScratchAllocationAfterForTests(int64_t successes);
size_t fontScratchLargestAllocationForTests();
}
