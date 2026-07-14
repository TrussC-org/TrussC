// =============================================================================
// sokol backend implementation (Android)
// sokol_app_tc.h owns the sapp_* implementation on Android; sokol_app.h is
// included declarations-only (no SOKOL_APP_IMPL).
// =============================================================================
// NOTE: SOKOL_NO_ENTRY is NOT defined on Android.
// sokol_app_tc.h exports ANativeActivity_onCreate() as the entry point,
// which calls sokol_main() (defined below) to get the sapp_desc.
// sapp_run() is NOT supported on Android.
// =============================================================================

#ifdef __ANDROID__

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

// ---------------------------------------------------------------------------
// Android entry point bridge
// ---------------------------------------------------------------------------
// sokol_app_tc.h's exported ANativeActivity_onCreate calls sokol_main() to
// get the sapp_desc (declared by sokol_app.h, defined by nobody else).
//
// Strategy: user's main() calls runApp<tcApp>() which stores the descriptor
// in trussc::internal::g_androidDesc. sokol_main() calls main() then returns
// that stored descriptor.
//
// This lets existing main.cpp work unchanged on Android.
// ---------------------------------------------------------------------------
extern int main();
namespace trussc { namespace internal { extern sapp_desc g_androidDesc; } }

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    main();  // Populates trussc::internal::g_androidDesc via runApp()
    return trussc::internal::g_androidDesc;
}

#endif // __ANDROID__
