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

#include "tcImageDecode.h"
#include "tcFontScratch.h"
#include "tcFontInput.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
void* imageMalloc(size_t size) {
    if (!trussc::internal::imageDecodeAllocationAllowed(size)) return nullptr;
    auto* result = std::malloc(size);
    if (!result) trussc::internal::imageDecodeAllocationFailed();
    return result;
}
void* imageRealloc(void* pointer, size_t size) {
    if (!trussc::internal::imageDecodeAllocationAllowed(size)) return nullptr;
    auto* result = std::realloc(pointer, size);
    if (!result && size) trussc::internal::imageDecodeAllocationFailed();
    return result;
}
}
#define STBI_MALLOC(size) imageMalloc(size)
#define STBI_REALLOC(pointer, size) imageRealloc(pointer, size)
#define STBI_FREE(pointer) std::free(pointer)

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
}
FontScratchScope::FontScratchScope(size_t sourceBytes)
    : previous_(fontScratchScope), sourceBytes_(sourceBytes) {
    fontScratchScope = this;
}
FontScratchScope::~FontScratchScope() { fontScratchScope = previous_; }
size_t fontScratchSourceBytes() { return fontScratchScope ? fontScratchScope->sourceBytes_ : SIZE_MAX; }
bool fontScratchFailed() { return fontScratchScope && fontScratchScope->failed(); }
void fontScratchFailure() { if (fontScratchScope) fontScratchScope->failed_ = true; }
void failFontScratchAllocationAfterForTests(int64_t successes) {
    fontScratchFailAfter = successes;
    fontScratchLargest = 0;
}
size_t fontScratchLargestAllocationForTests() { return fontScratchLargest; }
void* fontScratchMalloc(size_t bytes) {
    constexpr size_t overhead = TC_STBTT_ALLOC_PADDING;
    // Bound (representation): allocation bytes plus padding must fit size_t; overflow rejects the glyph.
    if (fontScratchFailed() || bytes > SIZE_MAX - overhead ||
        bytes > tcStbttMallocMax.load(std::memory_order_relaxed) || fontScratchFailAfter == 0) {
        fontScratchFailure(); return nullptr;
    }
    fontScratchLargest = std::max(fontScratchLargest, bytes);
    auto* result = std::malloc(bytes + overhead);
    if (!result) { fontScratchFailure(); return nullptr; }
    if (fontScratchFailAfter > 0) --fontScratchFailAfter;
    std::memset(static_cast<char*>(static_cast<void*>(result)) + bytes, 0, TC_STBTT_ALLOC_PADDING);
    return result;
}
void fontScratchFree(void* pointer) {
    std::free(pointer);
}
}
#define STBTT_malloc(x,u)  ((void)(u), trussc::internal::fontScratchMalloc(x))
#define STBTT_free(x,u)    ((void)(u), trussc::internal::fontScratchFree(x))
#define STBTT_VALIDATE_GLYPH(info, glyph, rejected) ((!(rejected) && !trussc::internal::fontScratchFailed() && trussc::internal::fontGlyphRangeFits(info, trussc::internal::fontScratchSourceBytes(), glyph)) ? true : (trussc::internal::fontScratchFailure(), false))

#ifdef _MSC_VER
#  pragma warning(push)
// stb's size_t-to-int limit expressions contain an unselected truncating cast
// on 64-bit MSVC. Cover the include and our expansions of those vendor macros.
#  pragma warning(disable: 4310)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

static_assert(STBTT_MAX_OVERSAMPLE == 8, "update FontAtlasManager prefilter admission with the backend");
static_assert(TC_STBTT_ALLOC_PADDING >= 2 * sizeof(stbtt_vertex),
              "STBTT_malloc padding must cover at least two stbtt_vertex");

static std::atomic<int> tcStbttMaxVerticesValue{STBTT_DEFAULT_MAX_VERTICES};
static std::atomic<int> tcStbttMaxPointsValue{STBTT_DEFAULT_MAX_POINTS};
static int tcStbttMaxVertices() {
    return tcStbttMaxVerticesValue.load(std::memory_order_relaxed);
}
static int tcStbttMaxPoints() {
    return tcStbttMaxPointsValue.load(std::memory_order_relaxed);
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

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

// Sound-related (stb_vorbis, dr_wav, dr_mp3) moved to
// modules/tcSound
