// =============================================================================
// sokol バックエンド実装 (Windows / Linux)
// =============================================================================

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY  // main() を自分で定義するため

// Suppress warnings from third-party sokol headers. They have intentional
// dead-code paths and unused helpers that trip -Wunused-* under -Wall/-Wextra.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "sokol_log.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "util/sokol_gl_tc.h"
#include "util/sokol_memtrack.h"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
