// =============================================================================
// sokol backend implementation (Windows)
// sokol_app_tc.h is the self-contained sapp_* implementation (main window,
// run loop, events, multi-window API).
// =============================================================================

#define SOKOL_NO_ENTRY  // main() is defined by the app

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

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
