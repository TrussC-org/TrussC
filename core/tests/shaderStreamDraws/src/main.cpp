#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "stream.glsl.h"

#include <cstdio>
#include <cstring>

using namespace tc;

namespace {
int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

struct InspectShader : Shader {
    sg_pipeline_desc description() { return createPipelineDesc(); }
};

struct InspectFullscreen : FullscreenShader {
    sg_pipeline_desc description() { return createPipelineDesc(); }
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

TC_CORE_TEST_MAIN(int argc, char** argv) {
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
