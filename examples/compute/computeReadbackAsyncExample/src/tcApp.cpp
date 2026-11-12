// =============================================================================
// computeReadbackAsyncExample - Read a GPU counter without blocking the app.
//
// readAsync delivers the result in a later update. Check the result before
// accessing its values; this path also works on WebGPU.
// =============================================================================

#include "tcApp.h"
#include "count.compute.glsl.h"

void tcApp::setup() {
    counter.allocate(sizeof(count), &count);
    compute.load(count_values_shader_desc);
    compute.setStorageBuffer(VIEW_counter, counter);
    compute.dispatch(4); // Count the integers 0..127 that are at least 64.
    counter.readAsync([this](ReadbackResult result) {
        if (result.ok()) count = result.get<uint32_t>(0);
    }); // The callback runs in a later update.
}

void tcApp::draw() {
    drawBitmapString("Values >= 64: " + to_string(count), 20, 30);
}
