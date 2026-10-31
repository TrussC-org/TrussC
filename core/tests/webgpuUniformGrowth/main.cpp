// Browser-owned Release test. Direct sg_desc setup is the test-only reserve seam;
// production WindowSettings and the public API need no additional setter.
#if !defined(__EMSCRIPTEN__)
#include <cstdio>
int main() {
    // Only without emdawn headers (see CMakeLists.txt); nothing is tested here.
    std::puts("SKIP webgpuUniformGrowth: emdawn WebGPU headers not found, native mock not built; run run.py in Chrome");
}
#else
#include <webgpu/webgpu.h>
#include <emscripten.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
bool failBuffer = false, failGroup = false, failAllocation = false;
int errors = 0, warnings = 0, failures = 0;
WGPUBuffer createBuffer(WGPUDevice device, const WGPUBufferDescriptor* desc) {
    if (failBuffer) return nullptr;
    return wgpuDeviceCreateBuffer(device, desc);
}
WGPUBindGroup createGroup(WGPUDevice device, const WGPUBindGroupDescriptor* desc) {
    return failGroup ? nullptr : wgpuDeviceCreateBindGroup(device, desc);
}
}
#define wgpuDeviceCreateBuffer createBuffer
#define wgpuDeviceCreateBindGroup createGroup
#define SOKOL_WGPU
#define SOKOL_IMPL
#include "sokol_gfx.h"
#undef wgpuDeviceCreateBuffer
#undef wgpuDeviceCreateBindGroup

namespace {
constexpr int width = 64, height = 32, frames = 6;
WGPUDevice device;
void check(bool ok, const char* label) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
void logger(const char*, uint32_t level, uint32_t, const char* message, uint32_t, const char*, void*) {
    if (level <= 1) ++errors;
    if (level == 2) ++warnings;
    if (message) std::printf("sokol[%u] %s\n", level, message);
}
void* allocate(size_t size, void*) { return failAllocation ? nullptr : std::malloc(size); }
void deallocate(void* ptr, void*) { std::free(ptr); }
void setup(int reserve) {
    sg_desc desc{};
    desc.environment.wgpu.device = device;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    desc.environment.defaults.sample_count = 1;
    desc.uniform_buffer_size = reserve;
    desc.logger.func = logger;
    desc.allocator.alloc_fn = allocate;
    desc.allocator.free_fn = deallocate;
    sg_setup(&desc);
}
std::vector<uint8_t> readBuffer(WGPUBuffer source, size_t size) {
    WGPUBufferDescriptor desc{};
    desc.size = size;
    desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &desc);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, size);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(_sg.wgpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands); wgpuCommandEncoderRelease(encoder);
    bool done = false, success = false;
    struct Result { bool* done; bool* success; } result{&done, &success};
    WGPUBufferMapCallbackInfo callback{};
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = &result;
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
        auto& r = *static_cast<Result*>(userdata);
        *r.success = status == WGPUMapAsyncStatus_Success; *r.done = true;
    };
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, size, callback);
    while (!done) emscripten_sleep(1);
    check(success, "readback maps");
    std::vector<uint8_t> bytes(size);
    if (success) std::memcpy(bytes.data(), wgpuBufferGetConstMappedRange(readback, 0, size), size);
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    return bytes;
}
std::vector<uint8_t> readPixels(sg_image image) {
    WGPUBufferDescriptor desc{};
    desc.size = width * height * 4;
    desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    WGPUTexelCopyTextureInfo source{};
    source.texture = _sg_lookup_image(image.id)->wgpu.tex;
    WGPUTexelCopyBufferInfo target{};
    target.buffer = buffer; target.layout.bytesPerRow = width * 4; target.layout.rowsPerImage = height;
    WGPUExtent3D extent{width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &target, &extent);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(_sg.wgpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands); wgpuCommandEncoderRelease(encoder);
    auto bytes = readBuffer(buffer, desc.size);
    wgpuBufferRelease(buffer);
    return bytes;
}
std::array<uint8_t, 4> color(int tile, int frame) {
    return {uint8_t((tile * 37 + frame * 11) % 251), uint8_t((tile * 71 + frame * 23) % 251),
        uint8_t((tile * 17 + frame * 47) % 251), 255};
}
struct Raster { sg_pipeline pipeline; sg_image image; sg_view view; };
// Tile shader with two uniform blocks and a readback-capable color target.
Raster makeRaster() {
    sg_shader_desc shaderDesc{};
    shaderDesc.vertex_func.source = R"(
        @group(0) @binding(3) var<uniform> tile: vec4<f32>;
        @vertex fn main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
            let positions = array<vec2<f32>,3>(vec2(0.,0.), vec2(2.,0.), vec2(0.,2.));
            return vec4(tile.xy + positions[index] * tile.zw, 0., 1.);
        })";
    shaderDesc.fragment_func.source = R"(
        @group(0) @binding(1) var<uniform> color: vec4<f32>;
        @fragment fn main() -> @location(0) vec4<f32> { return color; })";
    shaderDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shaderDesc.uniform_blocks[0].size = 16;
    shaderDesc.uniform_blocks[0].wgsl_group0_binding_n = 3;
    shaderDesc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shaderDesc.uniform_blocks[1].size = 16;
    shaderDesc.uniform_blocks[1].wgsl_group0_binding_n = 1; // reversed dynamic-offset order
    sg_shader shader = sg_make_shader(&shaderDesc);
    sg_pipeline_desc pipelineDesc{}; pipelineDesc.shader = shader;
    sg_pipeline pipeline = sg_make_pipeline(&pipelineDesc);
    sg_image_desc imageDesc{};
    imageDesc.width = width; imageDesc.height = height;
    imageDesc.usage.color_attachment = true; imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    // Sokol attachments omit CopySrc; inject a readback-capable owned fixture.
    WGPUTextureDescriptor textureDesc{};
    textureDesc.size = {width, height, 1}; textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.format = WGPUTextureFormat_RGBA8Unorm; textureDesc.mipLevelCount = 1; textureDesc.sampleCount = 1;
    textureDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture = wgpuDeviceCreateTexture(device, &textureDesc);
    imageDesc.wgpu_texture = texture;
    sg_image image = sg_make_image(&imageDesc);
    wgpuTextureRelease(texture);
    sg_view_desc viewDesc{}; viewDesc.color_attachment.image = image;
    sg_view view = sg_make_view(&viewDesc);
    return {pipeline, image, view};
}
std::vector<uint8_t> scene(int reserve) {
    setup(reserve);
    auto [pipeline, image, view] = makeRaster();
    sg_shader_desc computeDesc{};
    computeDesc.compute_func.source = R"(
        @group(0) @binding(0) var<uniform> params: vec4<u32>;
        @group(1) @binding(0) var<storage,read_write> result: array<u32>;
        @compute @workgroup_size(1) fn main() { result[params.x] = params.y; })";
    computeDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_COMPUTE;
    computeDesc.uniform_blocks[0].size = 16;
    computeDesc.views[0].storage_buffer.stage = SG_SHADERSTAGE_COMPUTE;
    sg_shader computeShader = sg_make_shader(&computeDesc);
    sg_pipeline_desc computePipelineDesc{}; computePipelineDesc.compute = true; computePipelineDesc.shader = computeShader;
    sg_pipeline computePipeline = sg_make_pipeline(&computePipelineDesc);
    sg_buffer_desc bufferDesc{}; bufferDesc.size = 256; bufferDesc.usage.storage_buffer = true;
    WGPUBufferDescriptor nativeBufferDesc{}; nativeBufferDesc.size = 256;
    nativeBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer nativeBuffer = wgpuDeviceCreateBuffer(device, &nativeBufferDesc);
    bufferDesc.wgpu_buffer = nativeBuffer;
    sg_buffer buffer = sg_make_buffer(&bufferDesc);
    wgpuBufferRelease(nativeBuffer);
    sg_view_desc storageDesc{}; storageDesc.storage_buffer.buffer = buffer;
    sg_view storage = sg_make_view(&storageDesc);
    std::vector<uint8_t> snapshots;
    for (int frame = 0; frame < frames; ++frame) {
        // First two frames grow repeatedly, later frames reuse the peak buffer.
        const int tileCount = frame == 0 ? 1024 : width * height;
        for (int passIndex = 0; passIndex < 3; ++passIndex) {
            sg_pass computePass{}; computePass.compute = true;
            sg_begin_pass(&computePass); sg_apply_pipeline(computePipeline);
            sg_bindings computeBindings{}; computeBindings.views[0] = storage;
            sg_apply_bindings(&computeBindings);
            for (int entry = 0; entry < 15; ++entry) {
                uint32_t params[4]{uint32_t(passIndex * 16 + entry), uint32_t(frame * 100 + passIndex * 20 + entry), 0, 0};
                sg_range range{params, sizeof(params)}; sg_apply_uniforms(0, &range); sg_dispatch(1, 1, 1);
            }
            sg_end_pass();
            sg_pass pass{}; pass.attachments.colors[0] = view;
            pass.action.colors[0].load_action = passIndex == 0 ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
            pass.action.colors[0].clear_value = {0, 0, 0, 1};
            sg_begin_pass(&pass); sg_apply_pipeline(pipeline);
            sg_bindings bindings{}; sg_apply_bindings(&bindings);
            for (int tile = passIndex; tile < tileCount; tile += 3) {
                const int x = tile % width, y = tile / width;
                // Scissor makes each distinct uniform block observable at one pixel.
                sg_apply_scissor_rect(x, y, 1, 1, true);
                float transform[4]{-1.f + 2.f*x/width, 1.f - 2.f*(y+1)/height, 2.f/width, 2.f/height};
                auto rgba = color(tile, frame);
                float tint[4]{rgba[0]/255.f, rgba[1]/255.f, rgba[2]/255.f, 1.f};
                sg_range vertex{transform, sizeof(transform)}, fragment{tint, sizeof(tint)};
                sg_apply_uniforms(0, &vertex); sg_apply_uniforms(1, &fragment); sg_draw(0, 3, 1);
            }
            sg_end_pass();
        }
        sg_commit();
        auto pixels = readPixels(image);
        for (int tile = 0; tile < width * height; ++tile) {
            auto expected = tile < tileCount ? color(tile, frame) : std::array<uint8_t,4>{0,0,0,255};
            if (std::memcmp(pixels.data() + tile * 4, expected.data(), 4)) { check(false, "analytic tile pixels"); break; }
        }
        auto computed = readBuffer(_sg_lookup_buffer(buffer.id)->wgpu.buf, 256);
        for (int passIndex = 0; passIndex < 3; ++passIndex) {
            for (int entry = 0; entry < 15; ++entry) {
                uint32_t value;
                std::memcpy(&value, computed.data() + (passIndex * 16 + entry) * 4, 4);
                check(value == uint32_t(frame * 100 + passIndex * 20 + entry), "compute uniforms across passes");
            }
        }
        snapshots.insert(snapshots.end(), pixels.begin(), pixels.end());
    }
    sg_shutdown();
    return snapshots;
}
#if !defined(TC_WEBGPU_BASELINE)
void faults() {
    for (int fault = 0; fault < 3; ++fault) {
        setup(256);
        auto& u = _sg.wgpu.uniform;
        auto oldBuffer = u.buf; auto oldStaging = u.staging; auto oldSize = u.num_bytes;
        const int before = errors;
        failAllocation = fault == 0; failBuffer = fault == 1;
        if (fault == 2) {
            _sg_shader_t shader{}; shader.wgpu.bgl_ub = reinterpret_cast<WGPUBindGroupLayout>(1);
            failGroup = true;
            check(!_sg_wgpu_uniform_bindgroup(&shader), "uniform bind group failure");
        } else {
            check(!_sg_wgpu_uniform_reserve(uint64_t(oldSize) + 256), "growth failure");
        }
        check(u.buf == oldBuffer && u.staging == oldStaging && u.num_bytes == oldSize, "transaction rollback");
        float data[4]{}; sg_range range{data, sizeof(data)};
        _sg_wgpu_apply_uniforms(0, &range);
        check(u.offset == 0 && errors == before + 1, "failed upload skipped; one Error");
        // Invalid sentinels deliberately catch any attempt to encode a failed draw.
        _sg.wgpu.rpass_enc = reinterpret_cast<WGPURenderPassEncoder>(1);
        _sg_wgpu_draw(0, 3, 1, 0, 0);
        _sg.wgpu.rpass_enc = nullptr;
        _sg.wgpu.cpass_enc = reinterpret_cast<WGPUComputePassEncoder>(1);
        _sg_wgpu_dispatch(1, 1, 1);
        _sg.wgpu.cpass_enc = nullptr;
        failAllocation = failBuffer = failGroup = false;
        _sg_wgpu_uniform_system_on_commit();
        _sg_wgpu_apply_uniforms(0, &range);
        check(u.offset == 256, "next frame recovers");
        sg_shutdown();
    }
    setup(256);
    _sg_shader_t shader{}; shader.slot.id = 1; shader.wgpu.bgl_view_smp = reinterpret_cast<WGPUBindGroupLayout>(1);
    _sg_pipeline_t pipeline{}; pipeline.slot.id = 1; pipeline.cmn.shader = _sg_shader_ref(&shader);
    _sg_bindings_ptrs_t bindings{}; bindings.pip = &pipeline;
    int before = errors;
    // Empty pool first: old code dereferenced the null result before checking it.
    auto& pool = _sg.wgpu.bindgroups_pool.pool;
    int savedTop = pool.queue_top; pool.queue_top = 0;
    check(!_sg_wgpu_apply_bindings_bindgroup(&bindings), "null cache creation fails safely");
    pool.queue_top = savedTop;
    check(errors == before + 1, "pool failure reports Error");
    failGroup = true;
    for (bool disabled : {false, true}) {
        _sg.desc.wgpu.disable_bindgroups_cache = disabled;
        for (int repeat = 0; repeat < 3; ++repeat)
            check(!_sg_wgpu_apply_bindings_bindgroup(&bindings), "group creation fails safely");
        check(pool.queue_top == savedTop, "failed cache creation releases pool slot");
    }
    failGroup = false;
    check(errors == before + 1, "cache failures report one Error across retries");
    sg_shutdown();
}
#endif
}
int main() {
    // Node CI has no browser adapter. run.py is the mandatory browser route.
    if (!EM_ASM_INT({ return typeof window !== 'undefined'; })) {
        std::puts("SKIP webgpuUniformGrowth: browser WebGPU required"); return 0;
    }
    device = emscripten_webgpu_get_device();
    auto large = scene(4 * 1024 * 1024);
    int before = warnings;
    auto tiny = scene(256);
    check(large == tiny, "tiny vs large pixels across six frames and three passes");
    #if !defined(TC_WEBGPU_BASELINE)
    check(warnings == before + 1, "one growth notice per backend");
    #else
    (void)before;
    #endif
    check(errors == 0, "normal scene has no errors");
    #if !defined(TC_WEBGPU_BASELINE)
    faults();
    #endif
    check(EM_ASM_INT({ return Module.webgpuValidationErrors || 0; }) == 0, "no WebGPU validation errors");
    wgpuDeviceRelease(device);
    std::printf("WEBGPU_UNIFORM_GROWTH: %s (6 frames, 3 raster/compute passes, tiny/large pixels, growth/prefix, allocation failures)\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
#endif
