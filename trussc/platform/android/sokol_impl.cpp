// =============================================================================
// sokol backend implementation (Android)
// =============================================================================
// NOTE: SOKOL_NO_ENTRY is NOT defined on Android.
// sokol_app.h provides ANativeActivity_onCreate() as the entry point,
// which calls sokol_main() to get the sapp_desc.
// sapp_run() is NOT supported on Android.
// =============================================================================

#ifdef __ANDROID__

#define SOKOL_IMPL

#include "sokol_log.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "util/sokol_gl_tc.h"
#include "util/sokol_memtrack.h"

// ---------------------------------------------------------------------------
// Android entry point bridge
// ---------------------------------------------------------------------------
// sokol_app.h (without SOKOL_NO_ENTRY) declares sokol_main() but does not
// define it. On Android, ANativeActivity_onCreate calls sokol_main() to get
// the sapp_desc.
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
