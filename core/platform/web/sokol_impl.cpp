// =============================================================================
// sokol バックエンド実装 (Emscripten / WebGPU or WebGL2)
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

// sokol_app.h declarations only — the web implementation of the sapp_* API
// (canvas, rAF frame loop, HTML5 events, WGPU/WebGL2 swapchain) lives in
// sokol_app_tc.h below.
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
