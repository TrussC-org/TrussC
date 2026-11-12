// =============================================================================
// computeReadbackExample - Read a GPU counter synchronously.
//
// A compute pass counts matching values; read() waits for the GPU result.
// Synchronous storage readback is unavailable on Web; use readAsync there.
// =============================================================================

#include "tcApp.h"
#include "count.compute.glsl.h"

void tcApp::setup() {
    counter.allocate(sizeof(count), &count);
    compute.load(count_values_shader_desc);
    compute.setStorageBuffer(VIEW_counter, counter);
    compute.dispatch(4); // Count the integers 0..127 that are at least 64.
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    counter.read(&count, sizeof(count)); // Blocks until the GPU finishes.
#endif
}

void tcApp::draw() {
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    drawBitmapString("Values >= 64: " + to_string(count), 20, 30);
#else
    drawBitmapString("Synchronous readback is unavailable on Web", 20, 30);
#endif
}
