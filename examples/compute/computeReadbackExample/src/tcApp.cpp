// =============================================================================
// computeReadbackExample - Read a GPU counter synchronously.
//
// A compute pass counts matching values; read() waits for the GPU result.
// Synchronous storage readback is unavailable on Web; use readAsync there.
// =============================================================================

#include "tcApp.h"
#include "count.compute.glsl.h"

void tcApp::setup() {
    // upload the starting value (0) so the count begins from zero
    counter.allocate(sizeof(count), &count);

    compute.load(count_values_shader_desc);

    // the shader's "counter" buffer is this StorageBuffer
    compute.setStorageBuffer(VIEW_counter, counter);

    // 4 groups x 32 threads (local_size_x) = thread IDs 0..127;
    // the 64 IDs >= 64 each add 1, so the answer is 64
    compute.dispatch(4);

#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    // copy the GPU value back into count; blocks until the GPU finishes
    counter.read(&count, sizeof(count));
#endif
}

void tcApp::draw() {
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    drawBitmapString("Values >= 64: " + to_string(count), 20, 30);
#else
    // Web can't wait for the GPU; see computeReadbackAsyncExample
    drawBitmapString("Synchronous readback is unavailable on Web", 20, 30);
#endif
}
