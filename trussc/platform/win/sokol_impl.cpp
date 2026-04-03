// =============================================================================
// sokol バックエンド実装 (Windows / Linux)
// =============================================================================

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY  // main() を自分で定義するため

#include "sokol_log.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "util/sokol_gl_tc.h"
#include "util/sokol_memtrack.h"
