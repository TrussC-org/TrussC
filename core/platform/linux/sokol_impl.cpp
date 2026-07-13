// =============================================================================
// sokol バックエンド実装 (Linux / Raspberry Pi)
// =============================================================================

#define SOKOL_NO_ENTRY  // main() を自分で定義するため

// Suppress warnings from third-party sokol headers. They have intentional
// dead-code paths and unused helpers that trip -Wunused-* under -Wall/-Wextra.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#if defined(SOKOL_GLCORE)
// Desktop Linux (X11 + GLX): sokol_app.h declarations only — the Linux
// implementation of the sapp_* API (main window, run loop, events) lives in
// sokol_app_tc.h below.
#include "sokol_app.h"
#define SOKOL_IMPL
#include "sokol_log.h"
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#else
// Raspberry Pi (GLES3 / EGL): still on the upstream sokol_app.h implementation
#define SOKOL_IMPL
#include "sokol_log.h"
#include "sokol_app.h"
#endif
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "util/sokol_gl_tc.h"
#include "util/sokol_memtrack.h"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
