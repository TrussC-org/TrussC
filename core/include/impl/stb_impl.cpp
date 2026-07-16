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

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

// Sound-related (stb_vorbis, dr_wav, dr_mp3) moved to
// modules/tcSound
