// =============================================================================
// computeReadbackExample - Read a GPU counter synchronously.
//
// A compute pass counts matching values; read() waits for the GPU result.
// Synchronous storage readback is unavailable on Web; use readAsync there.
// =============================================================================

#include "tcApp.h"
#include "sum.compute.glsl.h"

void tcApp::setup() {
    // upload the starting value (0) so the count begins from zero
    counter.allocate(sizeof(count), &count);

    compute.load(sum_values_shader_desc);

    // the shader's "counter" buffer is this StorageBuffer
    compute.setStorageBuffer(VIEW_counter, counter);

    // 4 groups x 32 threads (local_size_x) = 128 threads;
    // the first 100 each add their number, so the answer is 1 + 2 + ... + 100 = 5050
    compute.dispatch(4);

#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    // copy the GPU value back into count; blocks until the GPU finishes
    counter.read(&count, sizeof(count));
#endif
}

void tcApp::draw() {
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    drawBitmapString("Sum of 1..100: " + to_string(count), 20, 30);
#else
    // Web can't wait for the GPU; see computeReadbackAsyncExample
    drawBitmapString("Synchronous readback is unavailable on Web", 20, 30);
#endif
}
