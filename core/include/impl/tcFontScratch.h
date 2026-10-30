#pragma once
#include <cstddef>
#include <cstdint>

namespace trussc::internal {
// Internal per-glyph workspace, independent of app-produced atlas pages.
class FontScratchScope {
public:
    explicit FontScratchScope(size_t sourceBytes);
    ~FontScratchScope();
    FontScratchScope(const FontScratchScope&) = delete;
    FontScratchScope& operator=(const FontScratchScope&) = delete;
    bool failed() const { return failed_; }
private:
    friend void* fontScratchMalloc(size_t);
    friend void fontScratchFailure();
    friend size_t fontScratchSourceBytes();
    FontScratchScope* previous_;
    size_t sourceBytes_;
    bool failed_ = false;
};
void* fontScratchMalloc(size_t bytes);
void fontScratchFree(void* pointer);
void fontScratchFailure();
bool fontScratchFailed();
size_t fontScratchSourceBytes();
// Tests of retained stb allocation-failure handling and glyph range checks.
void failFontScratchAllocationAfterForTests(int64_t successes);
size_t fontScratchLargestAllocationForTests();
}
