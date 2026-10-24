// Run the owned attachment matrices against the real coordinator with an
// initialized dummy device, including resized/released target restoration.
#if defined(__linux__) && !defined(__ANDROID__)
#undef SOKOL_GLCORE
#undef SOKOL_GLES3
#define SOKOL_DUMMY_BACKEND
#define SOKOL_GFX_IMPL
#include "sokol/sokol_gfx.h"
#undef SOKOL_GFX_IMPL
#define SOKOL_GL_IMPL
#include "sokol/util/sokol_gl_tc.h"
#undef SOKOL_GL_IMPL
#define TC_WINDOW_ATTACH_DUMMY
#include "../eventDisconnectAllocation/src/main.cpp"

// Generated native shader descriptors do not support the dummy backend.
namespace trussc::internal {
sg_shader sglPremultShader() { return {}; }
sg_shader sglCoverageShader() { return {}; }
}
#else
#include <cstdio>
int main() { std::puts("SKIP: Linux dummy GPU attachment fixture"); }
#endif
