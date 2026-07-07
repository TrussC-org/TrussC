// =============================================================================
// sokol バックエンド実装 (macOS / Metal)
// =============================================================================

#define SOKOL_NO_ENTRY  // main() を自分で定義するため

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

// sokol_app.h: declarations only — the macOS implementation of the sapp_* API
// (main window, run loop, events) lives in sokol_app_tc.h below.
#include "sokol_app.h"

#define SOKOL_IMPL
#include "sokol_log.h"
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "util/sokol_gl_tc.h"
#include "util/sokol_memtrack.h"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
