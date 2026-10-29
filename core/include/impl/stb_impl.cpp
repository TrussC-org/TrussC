// =============================================================================
// stb library implementation
// =============================================================================

// Suppress warnings from third-party stb headers. They use C-isms (zero-init
// via { 0 }, etc.) that trip -Wmissing-field-initializers and friends.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

// Treat filenames as UTF-8 on Windows (stb converts to the wide API
// internally). No effect on other platforms. Callers pass UTF-8 via
// internal::pathToUtf8() — see tc/utils/tcFileIO.h.
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8

#include "tcMcpImageAdmission.h"
#include "tcImageDecode.h"
#include "tcFontScratch.h"
#include "tcFontInput.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace trussc::mcp::internal {
namespace {
struct alignas(std::max_align_t) ImageAllocation {
    ImageAllocationScope* owner;
    size_t size;
    ImageAllocation* previous;
    ImageAllocation* next;
};
thread_local ImageAllocationScope* imageAllocationScope = nullptr;
}
ImageAllocationScope::ImageAllocationScope(size_t limit)
    : previous_(imageAllocationScope), limit_(limit) { imageAllocationScope = this; }
ImageAllocationScope::~ImageAllocationScope() {
    while (first_) mcpImageFree(static_cast<ImageAllocation*>(first_) + 1);
    imageAllocationScope = previous_;
}
void* mcpImageMalloc(size_t size) {
    if (!trussc::internal::imageDecodeAllocationAllowed(size)) return nullptr;
    auto* scope = imageAllocationScope;
    if (!scope) {
        auto* result = std::malloc(size);
        if (!result) trussc::internal::imageDecodeAllocationFailed();
        return result;
    }
    // Bound (abi): codec allocation headers must fit size_t. Overflow returns null and is counted.
    if (size > SIZE_MAX - sizeof(ImageAllocation)) { scope->needed_ = SIZE_MAX; return nullptr; }
    const auto total = size + sizeof(ImageAllocation);
    // Bound (security): codec allocations must fit pre-admitted workspace. Overflow returns null and is counted.
    if (total > scope->limit_ - scope->used_) {
        const auto needed = total > SIZE_MAX - scope->used_ ? SIZE_MAX : total + scope->used_;
        scope->needed_ = std::max(scope->needed_, needed);
        return nullptr;
    }
    auto* block = static_cast<ImageAllocation*>(std::malloc(total));
    if (!block) { trussc::internal::imageDecodeAllocationFailed(); return nullptr; }
    block->owner = scope;
    block->size = size;
    block->previous = nullptr;
    block->next = static_cast<ImageAllocation*>(scope->first_);
    if (block->next) block->next->previous = block;
    scope->first_ = block;
    scope->used_ += total;
    scope->peak_ = std::max(scope->peak_, scope->used_);
    return block + 1;
}
void mcpImageFree(void* pointer) {
    if (!pointer) return;
    if (!imageAllocationScope) { std::free(pointer); return; }
    auto* block = static_cast<ImageAllocation*>(pointer) - 1;
    auto* scope = block->owner;
    if (block->previous) block->previous->next = block->next;
    else scope->first_ = block->next;
    if (block->next) block->next->previous = block->previous;
    scope->used_ -= block->size + sizeof(ImageAllocation);
    std::free(block);
}
void* mcpImageRealloc(void* pointer, size_t size) {
    if (!imageAllocationScope) {
        if (!trussc::internal::imageDecodeAllocationAllowed(size)) return nullptr;
        auto* result = std::realloc(pointer, size);
        if (!result && size) trussc::internal::imageDecodeAllocationFailed();
        return result;
    }
    if (!pointer) return mcpImageMalloc(size);
    if (!size) { mcpImageFree(pointer); return nullptr; }
    const auto previousSize = (static_cast<ImageAllocation*>(pointer) - 1)->size;
    // Reserve both buffers before replacing: an allocation never publishes a prefix.
    auto* replacement = mcpImageMalloc(size);
    if (!replacement) return nullptr;
    std::memcpy(replacement, pointer, std::min(previousSize, size));
    mcpImageFree(pointer);
    return replacement;
}
}

#define STBI_MALLOC(size) trussc::mcp::internal::mcpImageMalloc(size)
#define STBI_REALLOC(pointer, size) trussc::mcp::internal::mcpImageRealloc(pointer, size)
#define STBI_FREE(pointer) trussc::mcp::internal::mcpImageFree(pointer)

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_PERLIN_IMPLEMENTATION
#include "stb/stb_perlin.h"

// STBTT_assert does nothing in every build type, so Debug and Release builds
// handle font data the same way: stb's CFF buffer reads and seeks stay within
// the buffer either way. The stb source is not changed for this.
#define STBTT_assert(x) ((void)0)

// stb_truetype allocates through STBTT_malloc. Allocations are padded with
// TC_STBTT_ALLOC_PADDING (64) zeroed bytes after the requested size. The stb
// source is not changed for this; see core/include/stb/README.md.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define TC_STBTT_ALLOC_PADDING 64

// Limits that internal::setStbttLimitsForTests() lowers: the vertex limit of
// stb_truetype's CFF counting pass and its flattened point limit (stb's
// defaults, set after the include below), and the largest size STBTT_malloc
// allocates (no cap by default).
static std::atomic<size_t> tcStbttMallocMax{SIZE_MAX};
static int tcStbttMaxVertices();
static int tcStbttMaxPoints();
#define STBTT_MAX_VERTICES tcStbttMaxVertices()
#define STBTT_MAX_POINTS   tcStbttMaxPoints()

namespace trussc::internal {
namespace {
thread_local FontScratchScope* fontScratchScope = nullptr;
thread_local int64_t fontScratchFailAfter = -1;
thread_local size_t fontScratchLargest = 0;
struct alignas(std::max_align_t) FontScratchBlock { size_t bytes; FontScratchScope* owner; };
}
FontScratchScope::FontScratchScope(size_t sourceBytes, size_t pixelBytes)
    : previous_(fontScratchScope), sourceBytes_(sourceBytes) {
    // Bound (security): live outline/raster allocations and the metric-sized bitmap must fit the per-glyph working memory. Overflow rejects the whole glyph and is counted.
    constexpr size_t workspace = 256u * 1024u * 1024u;
    if (pixelBytes > workspace) { failed_ = true; limit_ = 0; }
    else limit_ = workspace - pixelBytes;
    fontScratchScope = this;
}
FontScratchScope::~FontScratchScope() { fontScratchScope = previous_; }
size_t fontScratchRemaining() {
    return fontScratchScope ? fontScratchScope->limit_ - fontScratchScope->used_ : SIZE_MAX;
}
size_t fontScratchSourceBytes() { return fontScratchScope ? fontScratchScope->sourceBytes_ : SIZE_MAX; }
bool fontScratchFailed() { return fontScratchScope && fontScratchScope->failed(); }
void fontScratchFailure() { if (fontScratchScope) fontScratchScope->failed_ = true; }
void failFontScratchAllocationAfterForTests(int64_t successes) {
    fontScratchFailAfter = successes;
    fontScratchLargest = 0;
}
size_t fontScratchLargestAllocationForTests() { return fontScratchLargest; }
void* fontScratchMalloc(size_t bytes) {
    constexpr size_t overhead = sizeof(FontScratchBlock) + TC_STBTT_ALLOC_PADDING;
    if (fontScratchFailed() || bytes > SIZE_MAX - overhead || bytes + overhead > fontScratchRemaining() ||
        bytes > tcStbttMallocMax.load(std::memory_order_relaxed) || fontScratchFailAfter == 0) {
        fontScratchFailure(); return nullptr;
    }
    fontScratchLargest = std::max(fontScratchLargest, bytes);
    auto* block = static_cast<FontScratchBlock*>(std::malloc(bytes + overhead));
    if (!block) { fontScratchFailure(); return nullptr; }
    if (fontScratchFailAfter > 0) --fontScratchFailAfter;
    block->bytes = bytes + overhead;
    block->owner = fontScratchScope;
    if (block->owner) block->owner->used_ += block->bytes;
    auto* result = block + 1;
    std::memset(static_cast<char*>(static_cast<void*>(result)) + bytes, 0, TC_STBTT_ALLOC_PADDING);
    return result;
}
void fontScratchFree(void* pointer) {
    if (!pointer) return;
    auto* block = static_cast<FontScratchBlock*>(pointer) - 1;
    if (block->owner) block->owner->used_ -= block->bytes;
    std::free(block);
}
}
#define STBTT_malloc(x,u)  ((void)(u), trussc::internal::fontScratchMalloc(x))
#define STBTT_free(x,u)    ((void)(u), trussc::internal::fontScratchFree(x))
#define STBTT_VALIDATE_GLYPH(info, glyph, rejected) ((!(rejected) && !trussc::internal::fontScratchFailed() && trussc::internal::fontGlyphRangeFits(info, trussc::internal::fontScratchSourceBytes(), glyph)) ? true : (trussc::internal::fontScratchFailure(), false))

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

static_assert(STBTT_MAX_OVERSAMPLE == 8, "update FontAtlasManager prefilter admission with the backend");
static_assert(TC_STBTT_ALLOC_PADDING >= 2 * sizeof(stbtt_vertex),
              "STBTT_malloc padding must cover at least two stbtt_vertex");

static std::atomic<int> tcStbttMaxVerticesValue{STBTT_DEFAULT_MAX_VERTICES};
static std::atomic<int> tcStbttMaxPointsValue{STBTT_DEFAULT_MAX_POINTS};
static int tcStbttMaxVertices() {
    return (int)std::min(size_t(tcStbttMaxVerticesValue.load(std::memory_order_relaxed)), trussc::internal::fontScratchRemaining() / sizeof(stbtt_vertex));
}
static int tcStbttMaxPoints() {
    return (int)std::min(size_t(tcStbttMaxPointsValue.load(std::memory_order_relaxed)), trussc::internal::fontScratchRemaining() / (sizeof(stbtt__point) + sizeof(stbtt__edge)));
}

// Test hooks, declared in tc/graphics/tcFont.h.
namespace trussc {
namespace internal {
// Limits above the defaults are clamped to them.
void setStbttLimitsForTests(int maxVertices, int maxPoints, size_t mallocMax) {
    tcStbttMaxVerticesValue.store(std::min(maxVertices, (int)STBTT_DEFAULT_MAX_VERTICES),
                                  std::memory_order_relaxed);
    tcStbttMaxPointsValue.store(std::min(maxPoints, (int)STBTT_DEFAULT_MAX_POINTS),
                                std::memory_order_relaxed);
    tcStbttMallocMax.store(mallocMax, std::memory_order_relaxed);
}
void resetStbttLimitsForTests() {
    setStbttLimitsForTests(STBTT_DEFAULT_MAX_VERTICES, STBTT_DEFAULT_MAX_POINTS, SIZE_MAX);
}
} // namespace internal
} // namespace trussc

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

// Sound-related (stb_vorbis, dr_wav, dr_mp3) moved to
// modules/tcSound
