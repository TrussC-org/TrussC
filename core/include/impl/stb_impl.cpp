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

// Returns nullptr when the size cannot be allocated; stb checks for that.
static void* tcStbttMalloc(size_t size) {
    if (size > SIZE_MAX - TC_STBTT_ALLOC_PADDING) return nullptr;
    if (size > tcStbttMallocMax.load(std::memory_order_relaxed)) return nullptr;
    void* p = std::malloc(size + TC_STBTT_ALLOC_PADDING);
    if (p) std::memset(static_cast<char*>(p) + size, 0, TC_STBTT_ALLOC_PADDING);
    return p;
}
#define STBTT_malloc(x,u)  ((void)(u), tcStbttMalloc(x))
#define STBTT_free(x,u)    ((void)(u), std::free(x))

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

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

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

// Sound-related (stb_vorbis, dr_wav, dr_mp3) moved to
// modules/tcSound
