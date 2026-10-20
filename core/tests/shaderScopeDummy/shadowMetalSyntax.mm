// Compile the real host checks against Metal's implementation and SDK, without
// adding a Sokol API or depending on a display/device at test execution time.
#define SOKOL_METAL
#define SOKOL_GFX_IMPL
#include "sokol/sokol_gfx.h"
#include "../../platform/metal/tcShadowAllocation.h"
