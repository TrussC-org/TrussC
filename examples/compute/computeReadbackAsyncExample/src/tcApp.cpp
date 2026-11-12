// =============================================================================
// computeReadbackAsyncExample - Read a GPU counter without blocking the app.
//
// readAsync delivers the result in a later update. Check the result before
// accessing its values; this path also works on WebGPU.
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

    // request a copy and return at once; the callback runs in a later update
    counter.readAsync([this](ReadbackResult result) {
        // ok() is false if the readback failed; don't touch the data then
        if (result.ok()) {
            // first uint32_t in the buffer
            count = result.get<uint32_t>(0);
        }
    });
}

void tcApp::draw() {
    // shows 0 for the first frames, then 64 once the callback has run
    drawBitmapString("Values >= 64: " + to_string(count), 20, 30);
}
