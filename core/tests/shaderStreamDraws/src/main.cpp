#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "stream.glsl.h"

#include <cstdio>
#include <cstring>
#include <array>
#if defined(SOKOL_WGPU)
#include <webgpu/webgpu.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif

using namespace tc;

namespace {
int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "TrussC", "%s: %s", name, ok ? "PASS" : "FAIL");
#endif
    if (!ok) ++failures;
}

struct InspectShader : Shader {
    sg_pipeline_desc description() { return createPipelineDesc(); }
};

struct InspectFullscreen : FullscreenShader {
    sg_pipeline_desc description() { return createPipelineDesc(); }
};

// Four pixels per column keep the sample well inside each slot's stripe.
constexpr int slotCount = 14;
constexpr int targetWidth = slotCount * 4;
bool samplerless = false;
bool samplerCompleted = false;
std::array<unsigned char, 4> slotColor(int slot) {
    return {static_cast<unsigned char>(17 + slot * 17),
            static_cast<unsigned char>(241 - slot * 13),
            static_cast<unsigned char>(31 + slot * 11), 255};
}

const sg_shader_desc* samplerlessDesc(sg_backend backend) {
    static sg_shader_desc desc;
    desc = *stream_samplerless_shader_desc(backend);
    // texelFetch becomes Metal read / HLSL Load / WGSL textureLoad. shdc
    // still reflects its unused GLSL sampler; non-GL validation allows its
    // removal. No sampler is declared in this shader's runtime descriptor.
    for (auto& sampler : desc.samplers) sampler = {};
    for (auto& pair : desc.texture_sampler_pairs) pair = {};
    return &desc;
}

class SamplerApp : public App {
public:
    void setup() override {
        sg_enable_stats();
        errors_ = getLogger().onLog.listen([](LogEventArgs& e) {
            if (e.level >= LogLevel::Error) check("sampler GPU error", false);
        });
        if (samplerless) {
            check("samplerless shader loads", shader_.load(samplerlessDesc));
            Pixels pixels;
            pixels.allocate(slotCount, 1, 4);
            for (int i = 0; i < slotCount; ++i) {
                const auto color = slotColor(i);
                std::memcpy(pixels.getData() + i * 4, color.data(), 4);
            }
            textures_[0].allocate(pixels);
            shader_.setTexture(0, textures_[0].getView(), sg_sampler{});
        } else {
            check("14-slot shader loads", shader_.load(stream_slots_shader_desc));
            for (int i = 0; i < slotCount; ++i) {
                Pixels pixels;
                pixels.allocate(1, 1, 4);
                const auto color = slotColor(i);
                std::memcpy(pixels.getData(), color.data(), 4);
                textures_[i].allocate(pixels);
                shader_.setTexture(i, textures_[i].getView(), textures_[i].getSampler());
            }
        }
#if defined(SOKOL_WGPU)
        // Ordinary Fbo textures do not request COPY_SRC on WebGPU. Give this
        // test's target that usage without changing the public Fbo API.
        sg_image_desc target = {};
        target.width = targetWidth;
        target.height = 4;
        target.sample_count = 1;
        target.pixel_format = SG_PIXELFORMAT_RGBA8;
        target.usage.color_attachment = true;
        target.usage.copy_src = true;
        target_ = sg_make_image(&target);
        sg_view_desc view = {};
        view.color_attachment.image = target_;
        attachment_ = sg_make_view(&view);
#else
        fbo_.allocate(targetWidth, 4);
#endif
    }

    void draw() override {
        if (!rendered_) {
            if (!shader_.isLoaded()) { exitApp(1); return; }
            ShaderVertex quad[] = {
                {-1, -1, 0, 0, 0, 0, 0, 0, 0}, {1, -1, 0, 0, 0, 0, 0, 0, 0},
                {1, 1, 0, 0, 0, 0, 0, 0, 0}, {-1, 1, 0, 0, 0, 0, 0, 0, 0}};
            const auto drawsBefore = sg_query_stats().cur_frame.num_draw;
#if defined(SOKOL_WGPU)
            sg_pass pass = {};
            pass.attachments.colors[0] = attachment_;
            pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            pass.action.colors[0].clear_value = {0, 0, 0, 1};
            // Shader submissions need a recording context. The raw-pass scope
            // alone is for direct sg_* draws and does not replay Shader queues.
            internal::RenderTarget target;
            target.passKind = internal::TargetPassKind::Fixed;
            target.isFbo = true;
            target.binding = pass;
            target.clearAction = pass.action;
            target.width = targetWidth;
            target.height = 4;
            target.colorFormat = SG_PIXELFORMAT_RGBA8;
            target.sampleCount = 1;
            target.hasDepth = false;
            target.depthFormat = SG_PIXELFORMAT_NONE;
            target.pooledResources = internal::leaseTargetResources(target.colorFormat, 1, target.depthFormat);
            {
                internal::TargetPassScope scope(target);
                shader_.submitVertices(quad, 4, PrimitiveType::Quads);
            }
#else
            fbo_.begin(0, 0, 0, 1);
            shader_.submitVertices(quad, 4, PrimitiveType::Quads);
            fbo_.end();
#endif
            check(samplerless ? "samplerless quad submitted (draw not skipped)"
                              : "14-slot sampler quad submitted (draw not skipped)",
                  sg_query_stats().cur_frame.num_draw == drawsBefore + 1);
            rendered_ = true;
#if defined(SOKOL_WGPU)
            // Public Fbo readPixels is synchronous and unsupported on WebGPU.
            // Append a test-only copy to the same encoder; map after commit.
            WGPUBufferDescriptor desc = {};
            desc.size = rowBytes * 4;
            desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            readback_ = wgpuDeviceCreateBuffer((WGPUDevice)sg_wgpu_device(), &desc);
            WGPUTexelCopyTextureInfo source = {};
            source.texture = (WGPUTexture)sg_wgpu_query_image_info(target_).tex;
            WGPUTexelCopyBufferInfo destination = {};
            destination.buffer = readback_;
            destination.layout.bytesPerRow = rowBytes;
            destination.layout.rowsPerImage = 4;
            WGPUExtent3D extent = {targetWidth, 4, 1};
            wgpuCommandEncoderCopyTextureToBuffer((WGPUCommandEncoder)sg_wgpu_command_encoder(),
                &source, &destination, &extent);
#else
            std::array<unsigned char, targetWidth * 4 * 4> pixels = {};
            check("sampler FBO readback succeeds", fbo_.readPixels(pixels.data()));
            verify(pixels.data(), targetWidth * 4);
#endif
        }
#if defined(SOKOL_WGPU)
        else if (!mapping_) {
            mapping_ = true;
            WGPUBufferMapCallbackInfo callback = {};
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void*, void*) {
                mapSuccess_ = status == WGPUMapAsyncStatus_Success;
                mapDone_ = true;
            };
            wgpuBufferMapAsync(readback_, WGPUMapMode_Read, 0, rowBytes * 4, callback);
        } else if (mapDone_) {
            check("sampler FBO readback succeeds", mapSuccess_);
            if (mapSuccess_) {
                verify(static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(readback_, 0, rowBytes * 4)), rowBytes);
                wgpuBufferUnmap(readback_);
            } else exitApp(1);
        }
#endif
    }

    void cleanup() override {
#if defined(SOKOL_WGPU)
        if (readback_) wgpuBufferRelease(readback_);
        sg_destroy_view(attachment_);
        sg_destroy_image(target_);
#endif
    }
private:
    void verify(const unsigned char* pixels, int stride) {
        for (int i = 0; i < slotCount; ++i) {
            const auto expected = slotColor(i);
            char label[80];
            std::snprintf(label, sizeof(label), "%s slot %d RGBA", samplerless ? "samplerless" : "sampler", i);
            const auto* actual = pixels + stride * 2 + (i * 4 + 2) * 4;
            std::printf("%s: actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n", label,
                actual[0], actual[1], actual[2], actual[3],
                expected[0], expected[1], expected[2], expected[3]);
            check(label, std::memcmp(actual, expected.data(), 4) == 0);
        }
        samplerCompleted = true;
        exitApp();
    }
    Shader shader_;
    Fbo fbo_;
    std::array<Texture, slotCount> textures_;
    EventListener errors_;
    bool rendered_ = false;
#if defined(SOKOL_WGPU)
    static constexpr int rowBytes = 256;
    sg_image target_ = {};
    sg_view attachment_ = {};
    WGPUBuffer readback_ = nullptr;
    bool mapping_ = false;
    inline static bool mapDone_ = false, mapSuccess_ = false;
#endif
};

// WebGPU has no Fbo pixel readback (tcFbo_web.cpp), so its GPU check stays a SKIP.
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3) || defined(SOKOL_D3D11) \
    || defined(SOKOL_METAL)
bool completed = false;

ShaderVertex vertex(float x, float y, float r, float g) {
    return {x, y, 0, 0, 0, r, g, 0, 1};
}

class GpuApp : public App {
public:
    void setup() override {
        check("gpu shader loads", shader_.load(stream_color_shader_desc));
        fbo_.allocate(32, 32);
        // The final triangle is the visible one and uses indices above 65536.
        large_.resize(70002, vertex(-2, -2, 1, 0));
        listener_ = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level == LogLevel::Error) ++allErrors_;
            if (e.level == LogLevel::Error && e.message.find("Shader stream could not allocate vertex/index storage for") != std::string::npos) {
                ++errors_;
                sizeReported_ = e.message.find("70002 vertices; this draw was skipped") != std::string::npos
                    && e.message.find("Shader::submitVertices()") != std::string::npos;
            }
        });
    }

    void draw() override {
        if (!shader_.isLoaded()) { exitApp(); return; }
        if (frame_ == 0) checkBindingRecovery();
        clear(0.0f);
        // Park a swapchain recording while separate FBO passes grow its stream.
        ShaderVertex triangle[] = {vertex(-1, -1, 1, 0), vertex(1, -1, 1, 0), vertex(0, 1, 1, 0)};
        shader_.begin();
        shader_.submitVertices(triangle, 3, PrimitiveType::Triangles);
        shader_.end();
        const auto captured = internal::currentWindowContext().currentTarget->deferredShaderDraws.back().bindings;

        if (frame_ == 1) large_.assign(300003, vertex(-2, -2, 0, 1));
        // Both the original buffer's left triangle and the replacement's tail
        // triangle must survive within one pass on the first and later frame.
        const size_t tail = large_.size() - 3;
        large_[tail] = vertex(0, -1, 0, 1);
        large_[tail + 1] = vertex(1, -1, 0, 1);
        large_[tail + 2] = vertex(0.5f, 1, 0, 1);
        ShaderVertex left[] = {vertex(-1, -1, 1, 0), vertex(0, -1, 1, 0), vertex(-0.5f, 1, 1, 0)};
        fbo_.begin(0, 0, 0, 1);
        shader_.submitVertices(left, 3, PrimitiveType::Triangles);
        shader_.submitVertices(large_.data(), static_cast<int>(large_.size()), PrimitiveType::Triangles);
        fbo_.end();
        checkPixel("pre-growth draw survives", 255, 0, 8);
        checkPixel(frame_ == 0 ? "first-frame large draw tail" : "later sudden growth tail", 0, 255, 24);

        // A separate pass consumes the same Shader's index stream. Only its
        // last rectangle is visible, so readback proves the tail was rendered.
        fbo_.begin(0, 0, 0, 1);
        ShaderVertex hidden[] = {vertex(-2, -2, 0, 1), vertex(-2, -2, 0, 1),
            vertex(-2, -2, 0, 1), vertex(-2, -2, 0, 1)};
        for (int i = 0; i < 11999; ++i) shader_.submitVertices(hidden, 4, PrimitiveType::Quads);
        ShaderVertex visible[] = {vertex(-1, -1, 0, 1), vertex(1, -1, 0, 1),
            vertex(1, 1, 0, 1), vertex(-1, 1, 0, 1)};
        shader_.submitVertices(visible, 4, PrimitiveType::Quads);
        fbo_.end();
        checkPixel("rectangle tail in second FBO pass", 0, 255);
        check("FBO flush preserves captured swapchain buffer handles",
            internal::currentWindowContext().currentTarget->deferredShaderDraws.back().bindings.vertex_buffers[0].id
                == captured.vertex_buffers[0].id);
        if (frame_++ == 0) {
            InspectShader moved(std::move(shader_));
            shader_ = std::move(moved);
        } else {
            check("successful growth emits no errors", errors_ == 0);
            checkAllocationFailure();
            // The queued swapchain draw still has to replay after clear().
            shader_.clear();
            completed = true;
            exitApp();
        }
    }

private:
    // Upstream #1644: an ALLOC buffer is legal to bind but skips drawing.
    // A corrected binding must recover within the same pass/pipeline.
    void checkBindingRecovery() {
        const sg_shader rawShader = sg_make_shader(stream_color_shader_desc(sg_query_backend()));
        auto desc = shader_.description();
        desc.shader = rawShader;
        desc.index_type = SG_INDEXTYPE_NONE;
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
        desc.sample_count = 1;
        const sg_pipeline pipeline = sg_make_pipeline(&desc);
        check("recovery pipeline valid", sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID);
        ShaderVertex triangle[] = {vertex(-1, -1, 0, 1), vertex(1, -1, 0, 1), vertex(0, 1, 0, 1)};
        sg_buffer_desc bufferDesc = {};
        bufferDesc.data = SG_RANGE(triangle);
        const sg_buffer validBuffer = sg_make_buffer(&bufferDesc);
        const sg_buffer pendingBuffer = sg_alloc_buffer();
        Texture color;
        color.allocate(32, 32, TextureFormat::RGBA8, TextureUsage::RenderTarget);
        sg_pass pass = {};
        pass.attachments.colors[0] = color.getAttachmentView();
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0, 0, 0, 1};
        for (bool recover : {false, true}) {
            {
                internal::TargetPassScope scope(pass, 32, 32, SG_PIXELFORMAT_RGBA8, 1, false);
                check("recovery pass active", scope.active());
                sg_apply_pipeline(pipeline);
                sg_bindings bindings = {};
                bindings.vertex_buffers[0] = pendingBuffer;
                sg_apply_bindings(&bindings);
                sg_draw(0, 3, 1);
                if (recover) {
                    bindings.vertex_buffers[0] = validBuffer;
                    sg_apply_bindings(&bindings);
                    // Deliberately no sg_apply_pipeline here.
                    sg_draw(0, 3, 1);
                }
            }
            fbo_.begin(0, 0, 0, 1);
            setupScreenOrtho();
            setColor(1);
            color.draw(0, 0);
            fbo_.end();
            checkPixel(recover ? "corrected binding draws without reapplying pipeline"
                               : "invalid binding skips draw", 0, recover ? 255 : 0);
        }
        sg_dealloc_buffer(pendingBuffer);
        sg_destroy_buffer(validBuffer);
        sg_destroy_pipeline(pipeline);
        sg_destroy_shader(rawShader);
    }

    void checkAllocationFailure() {
        InspectShader failing, another;
        check("failure-test shaders load", failing.load(stream_color_shader_desc) && another.load(stream_color_shader_desc));
        std::vector<sg_buffer> held;
        // query_stats publishes actual pool occupancy even with stats disabled.
        while (sg_query_stats().total.buffers.free) held.push_back(sg_alloc_buffer());
        // No slots remain for replacement buffers. Repeat the failing draw to
        // verify the core's process-wide Error gate rather than a test mock.
        std::vector<ShaderVertex> geometry(70002, vertex(-2, -2, 1, 0));
        geometry[69999] = vertex(-1, -1, 1, 0);
        geometry[70000] = vertex(1, -1, 1, 0);
        geometry[70001] = vertex(0, 1, 1, 0);
        fbo_.begin(0, 0, 0, 1);
        failing.submitVertices(geometry.data(), static_cast<int>(geometry.size()), PrimitiveType::Triangles);
        another.submitVertices(geometry.data(), static_cast<int>(geometry.size()), PrimitiveType::Triangles);
        fbo_.end();
        checkPixel("allocation failure skips draw", 0, 0);
        check("allocation failure emits one actionable Error", errors_ == 1 && allErrors_ == 1 && sizeReported_);
        for (auto buffer : held) sg_dealloc_buffer(buffer);
        fbo_.begin(0, 0, 0, 1);
        failing.submitVertices(geometry.data(), static_cast<int>(geometry.size()), PrimitiveType::Triangles);
        fbo_.end();
        checkPixel("allocation failure can recover in-frame", 255, 0);
    }

    void checkPixel(const char* name, int red, int green, int x = 16) {
        std::vector<unsigned char> pixels(32 * 32 * 4);
        check("FBO readback succeeds", fbo_.readPixels(pixels.data()));
        const size_t center = (16 * 32 + x) * 4;
        check(name, pixels[center] == red && pixels[center + 1] == green);
    }

    InspectShader shader_;
    Fbo fbo_;
    std::vector<ShaderVertex> large_;
    EventListener listener_;
    int frame_ = 0;
    int errors_ = 0;
    int allErrors_ = 0;
    bool sizeReported_ = false;
};
#endif
} // namespace

#if defined(__ANDROID__)
// NativeActivity calls main() without argv. Run the applicable new check by
// default; GLES cannot exercise samplerless bindings.
TC_CORE_TEST_MAIN() {
    int argc = 2;
    const char* argv[] = {"shaderStreamDraws", "--sampler-slots-check"};
    std::printf("SKIP samplerless: GL requires a sampler\n");
    __android_log_print(ANDROID_LOG_INFO, "TrussC", "SKIP samplerless: GL requires a sampler");
#else
TC_CORE_TEST_MAIN(int argc, char** argv) {
#endif
#if defined(__EMSCRIPTEN__)
    // Interactive browser recipe: ?mode=--sampler-slots-check (or samplerless).
    const char* browserMode = emscripten_run_script_string("new URLSearchParams(location.search).get('mode') || ''");
    const char* browserArgs[] = {"shaderStreamDraws", browserMode};
    if (browserMode[0]) { argc = 2; argv = const_cast<char**>(browserArgs); }
#endif
    if (argc > 1 && (std::strcmp(argv[1], "--sampler-slots-check") == 0
                    || std::strcmp(argv[1], "--samplerless-check") == 0)) {
        samplerless = std::strcmp(argv[1], "--samplerless-check") == 0;
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        if (samplerless) {
            std::printf("SKIP samplerless: GL requires a sampler\n");
            return 0;
        }
#endif
        WindowSettings settings;
        settings.setSize(targetWidth, 4);
        settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<SamplerApp>(settings, failures, [] {
            check(samplerless ? "samplerless checks completed" : "sampler slots checks completed", samplerCompleted);
            check(samplerless ? "samplerless verdict" : "sampler slots verdict", samplerCompleted && failures == 0);
        });
    }
    InspectShader shader;
    InspectFullscreen fullscreen;
    check("custom Shader pipelines use uint32 stream indices", shader.description().index_type == SG_INDEXTYPE_UINT32);
    check("fullscreen immutable indices remain uint16", fullscreen.description().index_type == SG_INDEXTYPE_UINT16);
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3) || defined(SOKOL_D3D11) \
    || defined(SOKOL_METAL)
        WindowSettings settings;
        settings.setSize(32, 32);
        settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<GpuApp>(settings, failures, [] {
            check("GPU checks completed", completed);
        });
#else
        std::printf("SKIP: GPU check needs Fbo pixel readback, which this backend lacks\n");
#endif
    }
    return failures ? 1 : 0;
}
