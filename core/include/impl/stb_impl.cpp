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

// stb_truetype allocates through STBTT_malloc. stbtt_GetGlyphShape sizes the
// TrueType vertex array as n + 2*numberOfContours and can read one element
// past it (a contour that starts with an off-curve point peeks at the next
// point, and for the last point of the glyph that is past the array). Every
// allocation gets zeroed padding after the requested size so that read stays
// inside the block and sees a fixed value. The stb source is not changed for
// this; see core/include/stb/README.md.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define TC_STBTT_ALLOC_PADDING 64

#ifdef TC_STBTT_TEST_LIMITS
// Test builds only (core/tests/fontSfntCheck/local.cmake defines this): the
// vertex limit of stb_truetype's CFF counting pass and a cap on STBTT_malloc
// sizes are variables the test sets, so both limits are reached with small
// fonts.
int tcStbttTestMaxVertices = 1 << 20;
size_t tcStbttTestMallocMax = SIZE_MAX;
#define STBTT_MAX_VERTICES tcStbttTestMaxVertices
#endif

// Returns nullptr when the size cannot be allocated; stb checks for that.
static void* tcStbttMalloc(size_t size) {
    if (size > SIZE_MAX - TC_STBTT_ALLOC_PADDING) return nullptr;
#ifdef TC_STBTT_TEST_LIMITS
    if (size > tcStbttTestMallocMax) return nullptr;
#endif
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

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

// Sound-related (stb_vorbis, dr_wav, dr_mp3) moved to
// modules/tcSound
