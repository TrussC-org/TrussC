// Interpose only calls from the app. This TU compiles the real platform backend;
// production headers and libraries contain no test allocation/trace switches.
#define sg_apply_uniforms metalGrowthOriginalApplyUniforms
#include "../../platform/mac/sokol_impl.mm"
#undef sg_apply_uniforms
#include "metalProbe.h"
#include <climits>
#include <cstring>

namespace {
int splitsRemaining = 0;
int splitCount = 0;
id<MTLBuffer> vertexBuffer;
bool expectFragment = false;
}

extern "C" void sg_apply_uniforms(int slot, const sg_range* data) {
    if (splitsRemaining && slot == 0 && data->size == 192) {
        // PBR's vertex block is 192 bytes. Put it in the final aligned slice,
        // so the immediately following fragment block MUST change buffers.
        _sg.mtl.cur_ub_offset = (int)[_sg.mtl.uniform_buffers[_sg.mtl.cur_frame_rotate_index] length] - 256;
        vertexBuffer = _sg.mtl.uniform_buffers[_sg.mtl.cur_frame_rotate_index];
        expectFragment = true;
        --splitsRemaining;
    }
    metalGrowthOriginalApplyUniforms(slot, data);
    if (expectFragment && slot == 1) {
        if (_sg.mtl.uniform_buffers[_sg.mtl.cur_frame_rotate_index] != vertexBuffer &&
            [_sg.mtl.idpool.pool indexOfObjectIdenticalTo:vertexBuffer] != NSNotFound)
            ++splitCount;
        expectFragment = false;
        vertexBuffer = nil;
    }
}

void metalGrowthReserve(int bytes) {
    // Called between frames' FBO work with no unsubmitted CPU recordings.
    // The queue-ordered synchronous flush completes all earlier submissions
    // before resetting every slot. After growth, other slots catch up normally
    // on their next first pass; no frame/rotation state is forged.
    _sg_mtl_ensure_command_buffer();
    sg_tc_mtl_flush();
    for (int slot = 0; slot < SG_NUM_INFLIGHT_FRAMES; ++slot) {
        id<MTLBuffer> buffer = [_sg.mtl.device newBufferWithLength:(NSUInteger)bytes
            options:MTLResourceCPUCacheModeWriteCombined | MTLResourceStorageModeShared];
        SOKOL_ASSERT(buffer);
        _sg_mtl_release_resource(_sg.frame_index,
            _sg_mtl_add_resource(_sg.mtl.uniform_buffers[slot]));
        _sg.mtl.uniform_buffers[slot] = buffer;
    }
    _sg.mtl.ub_size = bytes;
    _sg.mtl.cur_ub_offset = 0;
    _sg.mtl.cur_ub_base_ptr = nullptr;
}
void metalGrowthForceSplits(int count) { splitsRemaining = count; }
int metalGrowthSplitCount() { return splitCount; }
bool metalGrowthSlotsCaughtUp() {
    for (int slot = 0; slot < SG_NUM_INFLIGHT_FRAMES; ++slot)
        if ((int)[_sg.mtl.uniform_buffers[slot] length] != _sg.mtl.ub_size) return false;
    return true;
}

// Only newBufferWithLength:options: is reached during the injected operations.
@interface MetalGrowthFailDevice : NSObject
@end
@implementation MetalGrowthFailDevice
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    (void)length; (void)options;
    return nil;
}
@end

bool metalGrowthFailureChecks(sg_view attachment) {
    const auto slot = _sg.mtl.cur_frame_rotate_index;
    id<MTLBuffer> buffer = _sg.mtl.uniform_buffers[slot];
    id<MTLDevice> device = _sg.mtl.device;
    const int size = _sg.mtl.ub_size;
    const int offset = _sg.mtl.cur_ub_offset;
    auto* base = _sg.mtl.cur_ub_base_ptr;
    const int freeSlots = _sg.mtl.idpool.free_queue_top;
    const int queueFront = _sg.mtl.idpool.release_queue_front;
    _sg.mtl.device = (id<MTLDevice>)[[MetalGrowthFailDevice alloc] init];
    _sg_mtl_grow_uniform_buffer(16);
    bool ok = _sg.mtl.uniform_buffers[slot] == buffer && _sg.mtl.ub_size == size
        && _sg.mtl.cur_ub_offset == offset && _sg.mtl.cur_ub_base_ptr == base
        && _sg.mtl.idpool.free_queue_top == freeSlots
        && _sg.mtl.idpool.release_queue_front == queueFront;
    // Allocation must fail BEFORE retiring the previous slot, even on catch-up.
    _sg.mtl.cur_ub_base_ptr = nullptr;
    _sg.mtl.ub_size = size * 2;
    sg_pass pass = {};
    pass.attachments.colors[0] = attachment;
    sg_begin_pass(&pass);
    ok &= !_sg.cur_pass.valid && _sg.mtl.uniform_buffers[slot] == buffer
        && _sg.mtl.ub_size == size * 2 && _sg.mtl.cur_ub_offset == offset
        && _sg.mtl.cur_ub_base_ptr == nullptr
        && _sg.mtl.idpool.free_queue_top == freeSlots
        && _sg.mtl.idpool.release_queue_front == queueFront;
    sg_end_pass();
    _sg.mtl.device = device;
    _sg.mtl.ub_size = size;
    _sg.mtl.cur_ub_base_ptr = base;
    // A failed upload must poison only this draw, with old storage intact.
    // Call the private upload before shader lookup (failure returns first).
    unsigned char bytes[16] = {};
    sg_range range = {bytes, sizeof(bytes)};
    _sg.mtl.cur_ub_offset = size;
    _sg.next_draw_valid = true;
    _sg.mtl.device = (id<MTLDevice>)[[MetalGrowthFailDevice alloc] init];
    _sg_mtl_apply_uniforms(0, &range);
    ok &= !_sg.next_draw_valid && _sg.mtl.cur_ub_offset == size
        && _sg.mtl.uniform_buffers[slot] == buffer;
    _sg.mtl.device = device;
    _sg.mtl.cur_ub_offset = offset;
    // Checked-size rejection cannot attempt to allocate or change the cursor.
    _sg_mtl_grow_uniform_buffer(SIZE_MAX);
    ok &= _sg.mtl.uniform_buffers[slot] == buffer && _sg.mtl.ub_size == size
        && _sg.mtl.cur_ub_offset == offset;
    return ok;
}

bool metalGrowthComputeCheck() {
    metalGrowthReserve(256);
    sg_shader_desc desc = {};
    desc.compute_func.source =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void computeMain(constant uint4& a [[buffer(0)]], "
        "constant uint4& b [[buffer(3)]], device uint* result [[buffer(8)]], "
        "uint tid [[thread_position_in_grid]]) { if (tid==0) result[0]=a.x*100+b.x; }";
    desc.compute_func.entry = "computeMain";
    desc.mtl_threads_per_threadgroup = {32, 1, 1};
    for (int i = 0; i < 2; ++i) {
        desc.uniform_blocks[i].stage = SG_SHADERSTAGE_COMPUTE;
        desc.uniform_blocks[i].size = 16;
        desc.uniform_blocks[i].msl_buffer_n = i == 0 ? 0 : 3;
    }
    desc.views[0].storage_buffer.stage = SG_SHADERSTAGE_COMPUTE;
    desc.views[0].storage_buffer.msl_buffer_n = 8;
    sg_shader shader = sg_make_shader(&desc);
    sg_pipeline_desc pipelineDesc = {};
    pipelineDesc.compute = true;
    pipelineDesc.shader = shader;
    sg_pipeline pipeline = sg_make_pipeline(&pipelineDesc);
    sg_buffer_desc bufferDesc = {};
    bufferDesc.size = 16;
    bufferDesc.usage.storage_buffer = true;
    sg_buffer buffer = sg_make_buffer(&bufferDesc);
    sg_view_desc viewDesc = {};
    viewDesc.storage_buffer.buffer = buffer;
    sg_view view = sg_make_view(&viewDesc);
    sg_pass pass = {};
    pass.compute = true;
    sg_begin_pass(&pass);
    sg_apply_pipeline(pipeline);
    sg_bindings bindings = {};
    bindings.views[0] = view;
    sg_apply_bindings(&bindings);
    uint32_t a[4] = {7, 0, 0, 0}, b[4] = {9, 0, 0, 0};
    sg_range aRange = {a, sizeof(a)}, bRange = {b, sizeof(b)};
    sg_apply_uniforms(0, &aRange);
    // Observe the generation without retaining it across dispatch: production
    // retirement must keep the first block alive through GPU completion.
    const void* first = (__bridge const void*)_sg.mtl.uniform_buffers[_sg.mtl.cur_frame_rotate_index];
    sg_apply_uniforms(1, &bRange);
    const bool grew = first != (__bridge const void*)_sg.mtl.uniform_buffers[_sg.mtl.cur_frame_rotate_index];
    sg_dispatch(1, 1, 1);
    sg_end_pass();
    // Blit to shared staging, including on discrete/managed-storage Macs.
    const sg_mtl_buffer_info bufferInfo = sg_mtl_query_buffer_info(buffer);
    id<MTLBuffer> source = (__bridge id<MTLBuffer>)bufferInfo.buf[bufferInfo.active_slot];
    id<MTLBuffer> staging = [_sg.mtl.device newBufferWithLength:16 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [_sg.mtl.cmd_buffer blitCommandEncoder];
    [blit copyFromBuffer:source sourceOffset:0 toBuffer:staging destinationOffset:0 size:16];
    [blit endEncoding];
    sg_tc_mtl_flush();
    const bool ok = grew && ((const uint32_t*)[staging contents])[0] == 709;
    sg_destroy_view(view);
    sg_destroy_buffer(buffer);
    sg_destroy_pipeline(pipeline);
    sg_destroy_shader(shader);
    return ok;
}
