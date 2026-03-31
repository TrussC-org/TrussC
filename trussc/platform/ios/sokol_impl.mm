// =============================================================================
// sokol backend implementation (iOS / Metal)
// =============================================================================

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY  // main() is defined by the app

#include "sokol_log.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_gl.h"
#include "sokol_audio.h"
