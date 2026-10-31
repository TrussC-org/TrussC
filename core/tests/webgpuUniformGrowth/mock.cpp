// CPU regression of the real WebGPU backend using the pinned emdawn C headers.
// This checks growth, encoded resource lifetimes and synchronous allocation failures.
#include <webgpu/webgpu.h>
#include <vector>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct WGPUBufferImpl { std::vector<uint8_t> bytes; int references = 1; };
struct WGPUBindGroupImpl { WGPUBuffer buffer; };
namespace {
bool rejectBuffer = false, rejectGroup = false, rejectAllocation = false;
int liveBuffers = 0, errors = 0, warnings = 0, failures = 0;
WGPUBuffer boundBuffer = nullptr;
std::array<uint32_t, 2> boundOffsets{};
struct Draw { WGPUBuffer buffer; std::array<uint32_t, 2> offsets; int value; };
std::vector<Draw> recorded;
void check(bool ok, const char* label) { if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
}
extern "C" {
WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice, const WGPUBufferDescriptor* desc) {
    if (rejectBuffer) return nullptr;
    ++liveBuffers;
    auto buffer = new WGPUBufferImpl{std::vector<uint8_t>(desc->size), 1};
    return buffer;
}
void wgpuBufferRelease(WGPUBuffer buffer) { if (--buffer->references == 0) { --liveBuffers; delete buffer; } }
void wgpuQueueWriteBuffer(WGPUQueue, WGPUBuffer buffer, uint64_t offset, const void* data, size_t size) {
    check(offset <= buffer->bytes.size() && size <= buffer->bytes.size() - offset, "GPU write range");
    std::memcpy(buffer->bytes.data() + offset, data, size);
}
WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice, const WGPUBindGroupDescriptor* desc) {
    if (rejectGroup) return nullptr;
    WGPUBuffer buffer = desc->entryCount ? desc->entries[0].buffer : nullptr;
    if (buffer) ++buffer->references;
    return new WGPUBindGroupImpl{buffer};
}
void wgpuBindGroupRelease(WGPUBindGroup group) {
    if (group->buffer) wgpuBufferRelease(group->buffer);
    delete group;
}
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    if (index == 0 && group) {
        boundBuffer = group->buffer;
        for (size_t i = 0; i < count && i < boundOffsets.size(); ++i) boundOffsets[i] = offsets[i];
    }
}
void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    wgpuRenderPassEncoderSetBindGroup(nullptr, index, group, count, offsets);
}
void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder, uint32_t, uint32_t, uint32_t, uint32_t) {
    ++boundBuffer->references; // an encoded command retains its resources
    recorded.push_back({boundBuffer, boundOffsets, int(recorded.size())});
}
void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) { std::abort(); }
void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder, uint32_t, uint32_t, uint32_t) {
    wgpuRenderPassEncoderDraw(nullptr, 0, 0, 0, 0);
}
}
#define SOKOL_WGPU
#define SOKOL_IMPL
#include "sokol_gfx.h"
namespace {
void logger(const char*, uint32_t level, uint32_t, const char*, uint32_t, const char*, void*) {
    if (level <= 1) ++errors;
    if (level == 2) ++warnings;
}
void* allocate(size_t size, void*) { return rejectAllocation ? nullptr : std::malloc(size); }
void deallocate(void* ptr, void*) { std::free(ptr); }
}
int main() {
    _sg.desc.allocator = {allocate, deallocate, nullptr};
    _sg.desc.logger.func = logger;
    _sg.wgpu.valid = true;
    _sg.wgpu.dev = reinterpret_cast<WGPUDevice>(1);
    _sg.wgpu.rpass_enc = reinterpret_cast<WGPURenderPassEncoder>(1);
    _sg.wgpu.cpass_enc = reinterpret_cast<WGPUComputePassEncoder>(1);
    _sg.wgpu.limits.minUniformBufferOffsetAlignment = 256;
    _sg.wgpu.limits.maxBufferSize = 64 * 1024 * 1024;
    sg_desc desc{}; desc.uniform_buffer_size = 256; desc.wgpu.bindgroups_cache_size = 1024;
    _sg_wgpu_uniform_system_init(&desc);
    _sg_shader_t shaderSlots[3]{};
    _sg_shader_t& shader = shaderSlots[0]; shader.slot.id = 1;
    _sg.pools.shaders = shaderSlots; _sg.pools.shader_pool.size = 3;
    shader.cmn.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shader.cmn.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shader.wgpu.ub_num_dynoffsets = 2;
    shader.wgpu.ub_dynoffsets[0] = 1; shader.wgpu.ub_dynoffsets[1] = 0;
    shader.wgpu.ub_grp0_bnd_n[0] = 3; shader.wgpu.ub_grp0_bnd_n[1] = 1;
    shader.wgpu.bgl_ub = reinterpret_cast<WGPUBindGroupLayout>(1);
    _sg_pipeline_t pipeline{}; pipeline.slot.id = 1; pipeline.cmn.shader = _sg_shader_ref(&shader);
    _sg.cur_pip = _sg_pipeline_ref(&pipeline);
#if !defined(TC_WEBGPU_BASELINE)
    shaderSlots[1] = shader;
    check(_sg_wgpu_uniform_bindgroup(&shaderSlots[1]), "idle shader initial group");
    check(_sg_wgpu_uniform_bindgroup(&shaderSlots[2]), "empty shader initial group");
    const WGPUBindGroup emptyGroup = shaderSlots[2].wgpu.bg_ub;
#endif
#if defined(TC_WEBGPU_BASELINE)
    WGPUBindGroupEntry entry{}; entry.buffer = _sg.wgpu.uniform.buf;
    WGPUBindGroupDescriptor groupDesc{}; groupDesc.entryCount = 1; groupDesc.entries = &entry;
    shader.wgpu.bg_ub = wgpuDeviceCreateBindGroup(_sg.wgpu.dev, &groupDesc);
#endif
    for (int frame = 0; frame < 4; ++frame) {
        // An odd prefix forces a later growth between vertex/fragment uploads.
        int prefix[4]{}; sg_range range{prefix, sizeof(prefix)};
        _sg_wgpu_apply_uniforms(0, &range);
        for (int draw = 0; draw < 2048; ++draw) {
            int vertex[4]{draw, frame, 11, 12}, fragment[4]{draw+10000, frame, 21, 22};
            sg_range v{vertex, sizeof(vertex)}, f{fragment, sizeof(fragment)};
            _sg_wgpu_apply_uniforms(0, &v); _sg_wgpu_apply_uniforms(1, &f);
            _sg.cur_pass.is_compute = draw % 3 == 0;
            if (draw % 3) _sg_wgpu_draw(0, 3, 1, 0, 0);
            else _sg_wgpu_dispatch(1, 1, 1);
        }
        _sg_wgpu_uniform_system_on_commit();
#if !defined(TC_WEBGPU_BASELINE)
        check(shaderSlots[1].wgpu.bg_ub == nullptr, "idle shader old group retired");
        check(shaderSlots[2].wgpu.bg_ub == emptyGroup, "empty group survives growth");
#endif
        check(recorded.size() == 2048, "all draws/dispatches encoded");
        for (size_t index = 0; index < recorded.size(); ++index) {
            const Draw& draw = recorded[index];
            int vertex[4], fragment[4];
            std::memcpy(vertex, draw.buffer->bytes.data() + draw.offsets[1], sizeof(vertex));
            std::memcpy(fragment, draw.buffer->bytes.data() + draw.offsets[0], sizeof(fragment));
            check(vertex[0] == int(index) && fragment[0] == int(index)+10000 && vertex[1] == frame && fragment[1] == frame,
                "recorded uniforms survive growth and generation switches");
            wgpuBufferRelease(draw.buffer);
        }
        recorded.clear();
    }
#if !defined(TC_WEBGPU_BASELINE)
    check(errors == 0 && warnings == 1, "one growth warning and no normal errors");
    for (int fault = 0; fault < 4; ++fault) {
        auto& u = _sg.wgpu.uniform;
        WGPUBuffer previousBuffer = u.buf; uint8_t* previousStaging = u.staging;
        uint32_t previousSize = u.num_bytes; int previousLive = liveBuffers;
        rejectAllocation = fault == 0; rejectBuffer = fault == 1;
        if (fault == 2) {
            rejectGroup = true; ++u.generation;
            check(!_sg_wgpu_uniform_bindgroup(&shader), "uniform group failure");
            --u.generation;
        } else {
            uint64_t demand = fault == 3 ? UINT64_MAX : uint64_t(u.num_bytes) + 256;
            check(!_sg_wgpu_uniform_reserve(demand), "CPU/GPU/arithmetic failure");
        }
        check(u.buf == previousBuffer && u.staging == previousStaging && u.num_bytes == previousSize && liveBuffers == previousLive,
            "failed replacement rolls back and releases temporaries");
        int data[4]{}; sg_range range{data, sizeof(data)};
        _sg_wgpu_apply_uniforms(0, &range); _sg_wgpu_draw(0,3,1,0,0); _sg_wgpu_dispatch(1,1,1);
        check(u.offset == 0 && recorded.empty() && errors == 1, "failed uploads/draws/dispatches skipped; Error once");
        rejectAllocation = rejectBuffer = rejectGroup = false;
        _sg_wgpu_uniform_system_on_commit();
    }
    _sg.cur_pass.is_compute = false;
    const int cacheErrors = errors, cacheWarnings = warnings;
    _sg_wgpu_bindgroups_pool_init(&desc); _sg_wgpu_bindgroups_cache_init(&desc);
    shader.wgpu.bgl_view_smp = reinterpret_cast<WGPUBindGroupLayout>(1);
    _sg_bindings_ptrs_t bindings{}; bindings.pip = &pipeline;
    auto& pool = _sg.wgpu.bindgroups_pool.pool;
    const int freeSlots = pool.queue_top;
    pool.queue_top = 0;
    check(!_sg_wgpu_apply_bindings_bindgroup(&bindings), "null cache creation handled");
    pool.queue_top = freeSlots;
    rejectGroup = true;
    for (bool disabled : {false, true}) {
        _sg.desc.wgpu.disable_bindgroups_cache = disabled;
        for (int retry = 0; retry < 4; ++retry) check(!_sg_wgpu_apply_bindings_bindgroup(&bindings), "group creation failure handled");
        check(pool.queue_top == freeSlots, "failed cache group returns slot");
    }
    rejectGroup = false; _sg.desc.wgpu.disable_bindgroups_cache = false;
    check(errors == cacheErrors + 1, "cache failure Error once");
    for (uint32_t item = 1; item < 5000; ++item) {
        pipeline.slot.id = item;
        check(_sg_wgpu_apply_bindings_bindgroup(&bindings), "cache replacement succeeds");
    }
    check(errors == cacheErrors + 1 && warnings == cacheWarnings, "normal cache eviction quiet");
    _sg_wgpu_discard_all_bindgroups(); _sg_wgpu_bindgroups_cache_discard(); _sg_wgpu_bindgroups_pool_discard();
    for (auto& slot : shaderSlots) {
        if (slot.wgpu.bg_ub) { wgpuBindGroupRelease(slot.wgpu.bg_ub); slot.wgpu.bg_ub = nullptr; }
    }
#endif
    if (shader.wgpu.bg_ub) wgpuBindGroupRelease(shader.wgpu.bg_ub);
    _sg_wgpu_uniform_system_discard();
    check(liveBuffers == 0, "all buffer references retired");
    std::printf("WEBGPU_UNIFORM_MOCK: %s (8192 draws/dispatches, growth/prefix, allocation failures, cache failures/eviction)\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
