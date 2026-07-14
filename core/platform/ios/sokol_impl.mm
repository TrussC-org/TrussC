// =============================================================================
// sokol backend implementation (iOS / Metal)
// sokol_app_tc.h owns the sapp_* implementation on iOS; sokol_app.h is
// included declarations-only (no SOKOL_APP_IMPL).
// =============================================================================

#define SOKOL_NO_ENTRY  // main() is defined by the app

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "sokol_app.h"      // declarations only

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
