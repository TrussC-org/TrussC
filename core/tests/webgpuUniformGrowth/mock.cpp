// CPU regression of the real WebGPU backend using the pinned emdawn C headers.
// This checks encoded resource lifetimes and rollback without a browser/GPU.
#include <webgpu/webgpu.h>
#include <vector>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct WGPUBufferImpl { std::vector<uint8_t> bytes; int references = 1; bool error = false; };
struct WGPUBindGroupImpl { WGPUBuffer buffer; bool error; };
namespace {
// reject*: creation returns null. error*: creation returns an error object and
// the enclosing error scope reports the failure later, as WebGPU does.
bool rejectBuffer = false, rejectGroup = false, rejectAllocation = false;
bool errorBuffer = false, errorGroup = false;
// Using an error object raises a validation error, as in WebGPU: a queue write at
// once, a bind recorded in a pass at finish and again at submit. appError adds
// an unrelated encoding error (an app bug) to the next finish.
bool encoderInvalid = false, appError = false;
int liveBuffers = 0, errors = 0, warnings = 0, failures = 0, bufferCreations = 0, uncapturedErrors = 0;
struct Scope { WGPUErrorFilter filter; WGPUErrorType error; };
std::vector<Scope> scopes;
struct PendingScope { WGPUPopErrorScopeCallbackInfo info; WGPUErrorType type; };
std::vector<PendingScope> pendingScopes;
void reportError(WGPUErrorFilter filter, WGPUErrorType type) {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        if (scope->filter == filter) {
            if (scope->error == WGPUErrorType_NoError) scope->error = type;
            return;
        }
    }
    ++uncapturedErrors;
}
WGPUBuffer boundBuffer = nullptr;
std::array<uint32_t, 2> boundOffsets{};
struct Draw { WGPUBuffer buffer; std::array<uint32_t, 2> offsets; int value; };
std::vector<Draw> recorded;
void check(bool ok, const char* label) { if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
}
extern "C" {
void wgpuDevicePushErrorScope(WGPUDevice, WGPUErrorFilter filter) { scopes.push_back({filter, WGPUErrorType_NoError}); }
WGPUFuture wgpuDevicePopErrorScope(WGPUDevice, WGPUPopErrorScopeCallbackInfo info) {
    check(!scopes.empty(), "error scope pop matches a push");
    if (!scopes.empty()) {
        // Resolved later (asynchronously), in pop order.
        pendingScopes.push_back({info, scopes.back().error});
        scopes.pop_back();
    }
    return WGPUFuture{};
}
WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice, const WGPUBufferDescriptor* desc) {
    ++bufferCreations;
    if (rejectBuffer) return nullptr;
    ++liveBuffers;
    auto buffer = new WGPUBufferImpl{std::vector<uint8_t>(desc->size), 1};
    if (errorBuffer) { buffer->error = true; reportError(WGPUErrorFilter_OutOfMemory, WGPUErrorType_OutOfMemory); }
    return buffer;
}
void wgpuBufferRelease(WGPUBuffer buffer) { if (--buffer->references == 0) { --liveBuffers; delete buffer; } }
void wgpuQueueWriteBuffer(WGPUQueue, WGPUBuffer buffer, uint64_t offset, const void* data, size_t size) {
    if (buffer->error) { reportError(WGPUErrorFilter_Validation, WGPUErrorType_Validation); return; }
    check(offset <= buffer->bytes.size() && size <= buffer->bytes.size() - offset, "GPU write range");
    std::memcpy(buffer->bytes.data() + offset, data, size);
}
WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice, const WGPUBindGroupDescriptor* desc) {
    if (rejectGroup) return nullptr;
    WGPUBuffer buffer = desc->entryCount ? desc->entries[0].buffer : nullptr;
    if (buffer) ++buffer->references;
    const bool error = errorGroup || (buffer && buffer->error);
    if (error) reportError(WGPUErrorFilter_Validation, WGPUErrorType_Validation);
    return new WGPUBindGroupImpl{buffer, error};
}
void wgpuBindGroupRelease(WGPUBindGroup group) {
    if (group->buffer) wgpuBufferRelease(group->buffer);
    delete group;
}
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder, uint32_t index, WGPUBindGroup group, size_t count, const uint32_t* offsets) {
    if (group && group->error) encoderInvalid = true;
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
// Frame structure (begin pass, end pass, commit).
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice, const WGPUCommandEncoderDescriptor*) {
    encoderInvalid = false;
    return reinterpret_cast<WGPUCommandEncoder>(1);
}
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder, const WGPURenderPassDescriptor*) {
    return reinterpret_cast<WGPURenderPassEncoder>(1);
}
WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder, const WGPUComputePassDescriptor*) {
    return reinterpret_cast<WGPUComputePassEncoder>(1);
}
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder) {}
void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder) {}
void wgpuComputePassEncoderEnd(WGPUComputePassEncoder) {}
void wgpuComputePassEncoderRelease(WGPUComputePassEncoder) {}
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder, const WGPUCommandBufferDescriptor*) {
    const bool invalid = encoderInvalid || appError;
    if (invalid) reportError(WGPUErrorFilter_Validation, WGPUErrorType_Validation);
    encoderInvalid = false;
    return reinterpret_cast<WGPUCommandBuffer>(invalid ? 2 : 1);
}
void wgpuCommandEncoderRelease(WGPUCommandEncoder) {}
void wgpuQueueSubmit(WGPUQueue, size_t count, const WGPUCommandBuffer* commands) {
    for (size_t i = 0; i < count; ++i) {
        if (commands[i] == reinterpret_cast<WGPUCommandBuffer>(2)) reportError(WGPUErrorFilter_Validation, WGPUErrorType_Validation);
    }
}
void wgpuCommandBufferRelease(WGPUCommandBuffer) {}
}
#define SOKOL_WGPU
#define SOKOL_IMPL
#include "sokol_gfx.h"
namespace {
// forwarded: captured errors logged with their message; creationFailed: recovery Errors.
[[maybe_unused]] int forwarded = 0, creationFailed = 0;
void logger(const char*, uint32_t level, uint32_t item, const char*, uint32_t, const char*, void*) {
#if !defined(TC_WEBGPU_BASELINE)
    if (item == SG_LOGITEM_WGPU_UNIFORM_CHECK_CAPTURED_ERROR) ++forwarded;
    if (item == SG_LOGITEM_WGPU_UNIFORM_BUFFER_CREATION_FAILED) ++creationFailed;
#else
    (void)item;
#endif
    if (level <= 1) ++errors;
    if (level == 2) ++warnings;
}
void* allocate(size_t size, void*) { return rejectAllocation ? nullptr : std::malloc(size); }
void deallocate(void* ptr, void*) { std::free(ptr); }
// Delivers every popped scope's result, as the event loop would frames later.
void resolveScopes(size_t count = SIZE_MAX) {
    if (count > pendingScopes.size()) count = pendingScopes.size();
    std::vector<PendingScope> pending(pendingScopes.begin(), pendingScopes.begin() + count);
    pendingScopes.erase(pendingScopes.begin(), pendingScopes.begin() + count);
    for (auto& scope : pending) {
        scope.info.callback(WGPUPopErrorScopeStatus_Success, scope.type, WGPUStringView{nullptr, 0},
            scope.info.userdata1, scope.info.userdata2);
    }
}
void releaseRecorded() {
    for (const Draw& draw : recorded) wgpuBufferRelease(draw.buffer);
    recorded.clear();
}
#if !defined(TC_WEBGPU_BASELINE)
// One render pass per frame through the backend's frame boundary and commit.
sg_pass framePass{};
_sg_attachments_ptrs_t frameAttachments{};
// Outside these frames the mock keeps both pass encoders set (always "in a pass").
void beginFrame() {
    _sg.wgpu.rpass_enc = nullptr; _sg.wgpu.cpass_enc = nullptr;
    frameAttachments.empty = true;
    _sg_wgpu_begin_pass(&framePass, &frameAttachments);
}
void endFrame() {
    _sg_wgpu_end_pass(&frameAttachments); _sg_wgpu_commit();
    _sg.wgpu.rpass_enc = reinterpret_cast<WGPURenderPassEncoder>(1);
    _sg.wgpu.cpass_enc = reinterpret_cast<WGPUComputePassEncoder>(1);
}
#endif
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
        resolveScopes();
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
    check(scopes.empty(), "error scopes balanced");
    {
        // Asynchronous GPU failure: growth gets a non-null error object, and its
        // error scope reports OutOfMemory only after the frame was submitted.
        // Frames run through the backend's begin pass / end pass / commit, so
        // uploads, binds, draws, finish and submit really use the error object.
        _sg.cur_pass.is_compute = false;
        auto& u = _sg.wgpu.uniform;
        int data[4]{}; sg_range range{data, sizeof(data)};
        auto drawOnce = [&] { _sg_wgpu_apply_uniforms(0, &range); _sg_wgpu_apply_uniforms(1, &range); _sg_wgpu_draw(0,3,1,0,0); };
        auto frame = [&](int draws) { beginFrame(); for (int draw = 0; draw < draws; ++draw) drawOnce(); endFrame(); releaseRecorded(); };
        resolveScopes();
        frame(1); resolveScopes();
        frame(1); resolveScopes(); // the frame boundary released any confirmed predecessor
        WGPUBuffer good = u.buf; const uint32_t goodSize = u.num_bytes;
        const int goodLive = liveBuffers, goodErrors = errors, goodForwarded = forwarded, goodFailed = creationFailed;
        // One recovery Error since (e0, f0, c0); every other Error is a logged captured error.
        auto oneRecoveryError = [&](int e0, int f0, int c0) {
            return creationFailed == c0 + 1 && errors - forwarded == e0 - f0 + 1;
        };
        beginFrame();
        drawOnce();
        errorBuffer = true;
        for (int draw = 0; draw < 100000 && u.buf == good; ++draw) drawOnce();
        errorBuffer = false;
        check(u.buf != good && u.buf->error, "growth received a non-null error object");
        for (int draw = 0; draw < 8; ++draw) drawOnce();
        endFrame(); releaseRecorded();
        // The failure is reported a frame later; that frame still uses the error object.
        frame(1);
        resolveScopes();
        const int creationsBefore = bufferCreations;
        beginFrame();
        check(u.buf == good && u.num_bytes == goodSize, "next frame returns to the last good buffer and size");
        check(oneRecoveryError(goodErrors, goodForwarded, goodFailed), "asynchronous creation failure logs one Error");
        // R3: validation errors from using the error object are logged, not dropped.
        check(forwarded > goodForwarded, "captured errors from the failed frame are logged");
        drawOnce();
        check(shader.wgpu.bg_ub && !shader.wgpu.bg_ub->error && shader.wgpu.bg_ub->buffer == good,
            "uniform group from the error object is not reused");
        endFrame(); releaseRecorded(); resolveScopes();
        for (int count = 0; count < 4; ++count) { frame(1); resolveScopes(); }
        check(bufferCreations == creationsBefore && u.buf == good && oneRecoveryError(goodErrors, goodForwarded, goodFailed),
            "no retry loop while demand fits the restored buffer");
        check(liveBuffers == goodLive, "error object released after rollback");
        // Growth is retried only when demand needs it again, and then succeeds.
        beginFrame();
        for (int draw = 0; draw < 100000 && u.buf == good; ++draw) drawOnce();
        check(bufferCreations == creationsBefore + 1 && u.buf != good && !u.buf->error, "growth retried once on demand");
        endFrame(); releaseRecorded(); resolveScopes();
        frame(1);
        check(oneRecoveryError(goodErrors, goodForwarded, goodFailed) && liveBuffers == goodLive, "confirmed growth releases the previous buffer");
        resolveScopes();
        // A uniform bind group whose creation fails asynchronously is replaced.
        ++u.generation;
        beginFrame();
        errorGroup = true; drawOnce(); errorGroup = false;
        check(shader.wgpu.bg_ub && shader.wgpu.bg_ub->error, "group received a non-null error object");
        WGPUBuffer current = u.buf;
        endFrame(); releaseRecorded(); resolveScopes();
        beginFrame(); drawOnce();
        check(u.buf == current && shader.wgpu.bg_ub && !shader.wgpu.bg_ub->error, "errored group is not cached as valid");
        endFrame(); releaseRecorded(); resolveScopes();
        // A callback that fires after teardown must not affect a later backend.
        beginFrame();
        errorBuffer = true;
        for (int draw = 0; draw < 100000 && u.buf == current; ++draw) drawOnce();
        errorBuffer = false;
        endFrame(); releaseRecorded();
        _sg_wgpu_uniform_retire_groups();
        _sg.wgpu.valid = false;
        _sg_wgpu_uniform_system_discard();
        const size_t staleScopes = pendingScopes.size();
        _sg.wgpu.valid = true;
        u = {}; // sg_setup clears _sg, including the once-only diagnostics
        _sg_wgpu_uniform_system_init(&desc);
        const int freshErrors = errors;
        resolveScopes(staleScopes); // stale OutOfMemory results arrive before the new buffer's
        frame(1);
        resolveScopes();
        frame(1);
        check(errors == freshErrors && u.buf && !u.buf->error && !u.scope_failed, "stale callback ignored by a later backend");

        // A new backend (sg_setup clears _sg) with a confirmed buffer and no pending results.
        auto freshBackend = [&] {
            _sg_wgpu_uniform_retire_groups();
            _sg.wgpu.valid = false;
            _sg_wgpu_uniform_system_discard();
            resolveScopes();
            _sg.wgpu.valid = true;
            u = {};
            _sg_wgpu_uniform_system_init(&desc);
            frame(1); resolveScopes();
            frame(1); resolveScopes();
        };
        for (bool midFrame : {false, true}) {
            // R2: a later valid growth replaces an errored one before the next frame.
            // The valid buffer is kept, and the errored one is still reported, also
            // when its result arrives mid-frame (native event processing) before
            // the replacement.
            freshBackend();
            const int before = errors, live = liveBuffers, beforeForwarded = forwarded, beforeFailed = creationFailed;
            WGPUBuffer first = u.buf;
            beginFrame();
            drawOnce();
            errorBuffer = true;
            for (int draw = 0; draw < 100000 && u.buf == first; ++draw) drawOnce();
            errorBuffer = false;
            WGPUBuffer errored = u.buf;
            check(errored != first && errored->error, "R2 first growth received an error object");
            if (midFrame) resolveScopes();
            for (int draw = 0; draw < 100000 && u.buf == errored; ++draw) drawOnce();
            WGPUBuffer latest = u.buf; const uint32_t latestSize = u.num_bytes;
            check(latest != errored && !latest->error, "R2 further growth received a valid buffer");
            endFrame(); releaseRecorded(); resolveScopes();
            frame(1); resolveScopes();
            check(u.buf == latest && u.num_bytes == latestSize, "R2 later valid growth is kept");
            check(oneRecoveryError(before, beforeForwarded, beforeFailed), "R2 failure of a replaced generation logs one Error");
            check(liveBuffers == live, "R2 replaced and previous buffers released");
        }
        {
            // R2: frames without uniform uploads still reach the frame boundary.
            freshBackend();
            beginFrame();
            WGPUBuffer first = u.buf;
            for (int draw = 0; draw < 100000 && u.buf == first; ++draw) drawOnce();
            endFrame(); releaseRecorded(); resolveScopes();
            const int liveAfterGrowth = liveBuffers;
            beginFrame(); endFrame();
            check(liveBuffers == liveAfterGrowth - 1, "R2 confirmed predecessor released without uploads");
            // A shader without uniform blocks (its empty group 0 is created with the
            // shader) gets an error object; no frame uploads uniforms afterwards.
            const int before = errors, beforeForwarded = forwarded, beforeFailed = creationFailed;
            _sg_shader_t& empty = shaderSlots[2];
            empty = _sg_shader_t{}; empty.slot.id = 3;
            empty.wgpu.bgl_ub = reinterpret_cast<WGPUBindGroupLayout>(1);
            _sg_pipeline_t emptyPipeline{}; emptyPipeline.slot.id = 2; emptyPipeline.cmn.shader = _sg_shader_ref(&empty);
            _sg.cur_pip = _sg_pipeline_ref(&emptyPipeline);
            errorGroup = true;
            check(_sg_wgpu_uniform_bindgroup(&empty) && empty.wgpu.bg_ub->error, "R2 empty group received an error object");
            errorGroup = false;
            auto bindOnly = [&] {
                beginFrame();
                _sg_wgpu_uniform_system_on_apply_pipeline();
                _sg.wgpu.uniform.dirty = true; // bind group 0 as a draw would
                _sg_wgpu_uniform_system_set_bindgroup();
                endFrame();
            };
            bindOnly(); // the error object is used before its result arrives
            resolveScopes();
            for (int count = 0; count < 4; ++count) { bindOnly(); resolveScopes(); }
            check(oneRecoveryError(before, beforeForwarded, beforeFailed), "R2 empty group failure logs one Error without uploads");
            check(empty.wgpu.bg_ub && !empty.wgpu.bg_ub->error && !u.scope_failed, "R2 errored empty group is replaced");
            check(u.offset == 0 && recorded.empty(), "R2 no uploads or draws in these frames");
            _sg.cur_pip = _sg_pipeline_ref(&pipeline);
        }
        {
            // R2: an unrelated error inside a contained frame is forwarded, not hidden.
            freshBackend();
            const int before = errors, uncapturedBefore = uncapturedErrors;
            appError = true; frame(1); appError = false;
            // The app error reaches the uncaptured handler (at finish and submit), as without TrussC.
            check(uncapturedErrors > uncapturedBefore && errors == before,
                "R2 frames with only confirmed resources open no error scope");
            uncapturedErrors = uncapturedBefore; // that app error was injected on purpose
            beginFrame();
            WGPUBuffer first = u.buf;
            for (int draw = 0; draw < 100000 && u.buf == first; ++draw) drawOnce();
            WGPUBuffer grown = u.buf;
            appError = true; endFrame(); appError = false;
            releaseRecorded(); resolveScopes();
            check(errors == before + 1 && uncapturedErrors == uncapturedBefore,
                "R2 unrelated error in a contained frame is logged once, not uncaptured");
            frame(1); resolveScopes();
            check(u.buf == grown && errors == before + 1, "R2 unrelated error does not roll back a valid buffer");
        }
        {
            // R3: the initial buffer fails asynchronously, with no confirmed
            // predecessor. A shader without uniform blocks keeps its valid empty
            // group and draws; a shader with uniform blocks recreates the buffer.
            _sg_wgpu_uniform_retire_groups();
            _sg.wgpu.valid = false;
            _sg_wgpu_uniform_system_discard();
            resolveScopes();
            _sg.wgpu.valid = true;
            u = {};
            errorBuffer = true; _sg_wgpu_uniform_system_init(&desc); errorBuffer = false;
            const uint32_t initialSize = u.num_bytes;
            const int before = errors, beforeFailed = creationFailed;
            _sg_shader_t& empty = shaderSlots[2];
            empty = _sg_shader_t{}; empty.slot.id = 3;
            empty.wgpu.bgl_ub = reinterpret_cast<WGPUBindGroupLayout>(1);
            _sg_pipeline_t emptyPipeline{}; emptyPipeline.slot.id = 2; emptyPipeline.cmn.shader = _sg_shader_ref(&empty);
            _sg.cur_pip = _sg_pipeline_ref(&emptyPipeline);
            check(_sg_wgpu_uniform_bindgroup(&empty), "R3 empty group created on an unconfirmed initial buffer");
            WGPUBindGroup emptyGroup = empty.wgpu.bg_ub;
            resolveScopes();
            const int creations = bufferCreations;
            int bound = 0;
            for (int count = 0; count < 5; ++count) {
                beginFrame();
                _sg_wgpu_uniform_system_on_apply_pipeline();
                _sg.wgpu.uniform.dirty = true; // bind group 0 as a draw would
                if (_sg_wgpu_uniform_system_set_bindgroup()) ++bound;
                endFrame(); resolveScopes();
            }
            check(bound == 5 && empty.wgpu.bg_ub == emptyGroup,
                "R3 valid empty group kept and bound after an initial buffer failure");
            check(bufferCreations == creations, "R3 no buffer retry while nothing needs one");
            check(creationFailed == beforeFailed + 1 && errors == before + 1, "R3 initial buffer failure logs one Error");
            // A shader without uniform blocks created while no buffer exists gets
            // its empty group without creating a buffer.
            _sg_shader_t& emptyLate = shaderSlots[1];
            if (emptyLate.wgpu.bg_ub) { wgpuBindGroupRelease(emptyLate.wgpu.bg_ub); }
            emptyLate = _sg_shader_t{}; emptyLate.slot.id = 4;
            emptyLate.wgpu.bgl_ub = reinterpret_cast<WGPUBindGroupLayout>(1);
            check(!u.buf && _sg_wgpu_uniform_bindgroup(&emptyLate) && emptyLate.wgpu.bg_ub && !u.buf
                && bufferCreations == creations && errors == before + 1, "R3 empty group created without a buffer");
            // A shader with uniform blocks, created now, recreates the buffer at the
            // initial reservation instead of failing.
            _sg_shader_t& fresh = shaderSlots[1];
            if (fresh.wgpu.bg_ub) { wgpuBindGroupRelease(fresh.wgpu.bg_ub); }
            fresh = shader; fresh.slot.id = 2; fresh.wgpu.bg_ub = nullptr;
            check(_sg_wgpu_uniform_bindgroup(&fresh) && u.buf && !u.buf->error && u.num_bytes == initialSize
                && bufferCreations == creations + 1, "R3 a shader with uniform blocks recreates the missing buffer");
            _sg.cur_pip = _sg_pipeline_ref(&pipeline);
            frame(1); resolveScopes();
            check(errors == before + 1 && u.buf && !u.scope_failed, "R3 frames draw again after the recreation");
            // With a larger reservation, the first upload after such a failure
            // recreates the buffer at the initial reservation, not at its own size.
            _sg_wgpu_uniform_retire_groups();
            _sg.wgpu.valid = false;
            _sg_wgpu_uniform_system_discard();
            resolveScopes();
            _sg.wgpu.valid = true;
            u = {};
            sg_desc largeDesc = desc; largeDesc.uniform_buffer_size = 1024 * 1024;
            errorBuffer = true; _sg_wgpu_uniform_system_init(&largeDesc); errorBuffer = false;
            const uint32_t largeSize = u.num_bytes;
            resolveScopes();
            beginFrame();
            check(!u.buf, "R3 failed initial buffer dropped at the frame boundary");
            drawOnce();
            check(u.buf && !u.buf->error && u.num_bytes == largeSize, "R3 first upload recreates the initial reservation");
            endFrame(); releaseRecorded(); resolveScopes();
        }
        {
            // R3: a group that fails but is never bound does not hide an unrelated
            // app error captured in the same frame.
            freshBackend();
            const int beforeForwarded = forwarded, beforeFailed = creationFailed, uncapturedBefore = uncapturedErrors;
            beginFrame(); drawOnce();
            _sg_shader_t& unused = shaderSlots[2];
            unused = shader; unused.slot.id = 3; unused.wgpu.bg_ub = nullptr;
            errorGroup = true;
            check(_sg_wgpu_uniform_bindgroup(&unused) && unused.wgpu.bg_ub->error, "R3 unused group received an error object");
            errorGroup = false;
            appError = true; endFrame(); appError = false;
            releaseRecorded(); resolveScopes();
            frame(1); resolveScopes();
            check(forwarded == beforeForwarded + 1, "R3 unrelated app error logged although an unused group failed");
            check(creationFailed == beforeFailed + 1 && uncapturedErrors == uncapturedBefore, "R3 unused group failure logs one Error");
        }
        {
            // R3: error-scope results have no guaranteed settlement order. The same
            // frame logs the same errors whether the containment results arrive
            // after or before the creation results.
            int logged[2][3]{};
            for (bool containmentFirst : {false, true}) {
                freshBackend();
                const int e0 = errors, f0 = forwarded, c0 = creationFailed;
                beginFrame(); drawOnce();
                errorBuffer = true;
                _sg_wgpu_uniform_reserve(uint64_t(u.num_bytes) + 256);
                errorBuffer = false;
                drawOnce(); endFrame(); releaseRecorded();
                if (containmentFirst) {
                    std::vector<PendingScope> saved; saved.swap(pendingScopes);
                    for (auto& scope : saved) if (scope.info.callback == _sg_wgpu_uniform_use_callback) pendingScopes.push_back(scope);
                    for (auto& scope : saved) if (scope.info.callback != _sg_wgpu_uniform_use_callback) pendingScopes.push_back(scope);
                }
                resolveScopes();
                frame(1); resolveScopes();
                logged[containmentFirst][0] = errors - e0;
                logged[containmentFirst][1] = forwarded - f0;
                logged[containmentFirst][2] = creationFailed - c0;
                check(creationFailed == c0 + 1 && errors - e0 == forwarded - f0 + 1,
                    "R3 one recovery Error in either result order");
            }
            check(std::memcmp(logged[0], logged[1], sizeof(logged[0])) == 0 && logged[0][1] >= 1,
                "R3 the same errors are logged in either result order");
        }
        check(scopes.empty() && uncapturedErrors == 0, "error scopes balanced and capture every failure");
    }
    {
        // R4: two separate asynchronous failures of unused groups, each after the
        // previous recovery, report one recovery Error each (not once per backend).
        resolveScopes();
        const int c0 = creationFailed;
        for (int cycle = 0; cycle < 2; ++cycle) {
            beginFrame();
            auto& unused = shaderSlots[2];
            if (unused.wgpu.bg_ub) { wgpuBindGroupRelease(unused.wgpu.bg_ub); }
            unused = shader; unused.slot.id = 3; unused.wgpu.bg_ub = nullptr;
            errorGroup = true;
            check(_sg_wgpu_uniform_bindgroup(&unused), "R4 unused group returns error object");
            errorGroup = false;
            endFrame(); resolveScopes();
            beginFrame(); endFrame(); resolveScopes();
            check(creationFailed == c0 + cycle + 1, "R4 each separate failed generation reports one recovery Error");
        }
        check(scopes.empty(), "R4 error scopes balanced");
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
    _sg_wgpu_uniform_retire_groups();
#endif
    if (shader.wgpu.bg_ub) wgpuBindGroupRelease(shader.wgpu.bg_ub);
    _sg_wgpu_uniform_system_discard();
    check(liveBuffers == 0, "all buffer references retired");
    std::printf("WEBGPU_UNIFORM_MOCK: %s (8192 draws/dispatches, rollback, async GPU failure, cache failures/eviction)\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
