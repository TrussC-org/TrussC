// Test-only sokol implementation: platform app symbols without opening a
// window, and dummy graphics resources for model/texture loading tests.
// Supplying all sokol symbols keeps the platform implementation object in
// libTrussC from being linked into these executables.
#define SOKOL_NO_ENTRY
#define SOKOL_IMPL
#include "sokol/sokol_log.h"
#define SOKOL_APP_TC_IMPL
#include "sokol/sokol_app_tc.h"

#undef SOKOL_GLCORE
#undef SOKOL_GLES3
#undef SOKOL_D3D11
#undef SOKOL_METAL
#undef SOKOL_WGPU
#define SOKOL_DUMMY_BACKEND
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/util/sokol_gl_tc.h"
#include "sokol/util/sokol_memtrack.h"
