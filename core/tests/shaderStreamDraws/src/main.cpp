#include <TrussC.h>
#include "../../common/tcCoreTest.h"

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
    size_t vertexCapacity() const { return sg_query_buffer_size(vertexBuffer) / sizeof(ShaderVertex); }
    size_t indexCapacity() const { return sg_query_buffer_size(indexBuffer) / sizeof(uint32_t); }
};

struct InspectFullscreen : FullscreenShader {
    sg_pipeline_desc description() { return createPipelineDesc(); }
};

#if defined(SOKOL_GLCORE)
bool completed = false;

const sg_shader_desc* testShader(sg_backend backend) {
    if (backend != SG_BACKEND_GLCORE) return nullptr;
    static const sg_shader_desc desc = [] {
        sg_shader_desc d = {};
        d.vertex_func.source = "#version 330\nlayout(location=0) in vec3 position;"
            "layout(location=1) in vec2 uv; layout(location=2) in vec4 color;"
            "out vec4 c; void main(){gl_Position=vec4(position,1); c=color;}";
        d.fragment_func.source = "#version 330\nin vec4 c; out vec4 frag; void main(){frag=c;}";
        return d;
    }();
    return &desc;
}

ShaderVertex vertex(float x, float y, float r, float g) {
    return {x, y, 0, 0, 0, r, g, 0, 1};
}

class GpuApp : public App {
public:
    void setup() override {
        check("gpu shader loads", shader_.load(testShader));
        fbo_.allocate(32, 32);
        // The final triangle is the visible one and uses indices above 65536.
        large_.resize(70002, vertex(-2, -2, 1, 0));
        large_[69999] = vertex(-1, -1, 1, 0);
        large_[70000] = vertex(1, -1, 1, 0);
        large_[70001] = vertex(0, 1, 1, 0);
        listener_ = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level == LogLevel::Warning && e.message.find("Stream buffers grew to") != std::string::npos) {
                ++warnings_;
                sizeReported_ = e.message.find("262144 vertices and 262144 indices") != std::string::npos;
            }
        });
    }

    void draw() override {
        if (!shader_.isLoaded()) { exitApp(); return; }
        clear(0.0f);
        // Capture a swapchain draw before the FBO flush requests growth. Its
        // captured handles must stay the same throughout this sokol frame.
        ShaderVertex triangle[] = {vertex(-1, -1, 1, 0), vertex(1, -1, 1, 0), vertex(0, 1, 1, 0)};
        shader_.begin();
        shader_.submitVertices(triangle, 3, PrimitiveType::Triangles);
        shader_.end();
        const auto captured = internal::currentWindowContext().deferredShaderDraws.back().bindings;

        fbo_.begin(0, 0, 0, 1);
        shader_.submitVertices(large_.data(), static_cast<int>(large_.size()), PrimitiveType::Triangles);
        fbo_.end();
        checkPixel("large FBO draw", frame_ == 0 ? 0 : 255, 0);

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
        checkPixel("rectangle tail in second FBO pass", 0, frame_ == 0 ? 0 : 255);
        check("FBO flush preserves captured swapchain buffer handles",
            internal::currentWindowContext().deferredShaderDraws.back().bindings.vertex_buffers[0].id
                == captured.vertex_buffers[0].id);
        if (frame_++ == 0) {
            InspectShader moved(std::move(shader_));
            shader_ = std::move(moved);
        } else {
            check("growth warning reports new capacities once", warnings_ == 1 && sizeReported_);
            check("buffers cover all passes next frame",
                shader_.vertexCapacity() == 262144 && shader_.indexCapacity() == 262144);
            // The queued swapchain draw still has to replay after clear().
            shader_.clear();
            completed = true;
            exitApp();
        }
    }

private:
    void checkPixel(const char* name, int red, int green) {
        std::vector<unsigned char> pixels(32 * 32 * 4);
        check("FBO readback succeeds", fbo_.readPixels(pixels.data()));
        const size_t center = (16 * 32 + 16) * 4;
        check(name, pixels[center] == red && pixels[center + 1] == green);
    }

    InspectShader shader_;
    Fbo fbo_;
    std::vector<ShaderVertex> large_;
    EventListener listener_;
    int frame_ = 0;
    int warnings_ = 0;
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
#if defined(SOKOL_GLCORE)
        WindowSettings settings;
        settings.setSize(32, 32);
        settings.setHighDpi(false);
        runApp<GpuApp>(settings);
        check("GPU checks completed", completed);
#else
        std::printf("SKIP: GPU check uses OpenGL Core test shader\n");
#endif
    }
    return failures ? 1 : 0;
}
