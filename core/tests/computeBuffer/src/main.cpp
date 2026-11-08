#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "pattern.compute.glsl.h"
#include "reader.compute.glsl.h"
#include <array>
#include <cstdio>
#include <cstring>
#if defined(SOKOL_GLCORE)
#include <GL/gl.h>
#elif defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#endif
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
class BufferReader : public Shader {
    sg_pipeline_desc createPipelineDesc() override {
        auto desc = Shader::createPipelineDesc();
        desc.layout = {};
        return desc;
    }
};
void headlessChecks() {
    StorageBuffer buffer;
    check("headless allocation is refused", !buffer.allocate(64) && !buffer.isAllocated());
    const Vec4 value(1, 0, 0, 1);
    check("headless update is refused", !buffer.update(&value, sizeof(value)));
    ComputeShader compute;
    Shader graphics;
    check("headless compute binding is refused", !compute.setStorageBuffer(0, buffer));
    check("headless graphics binding is refused", !graphics.setStorageBuffer(0, buffer));
    check("headless checks leave GPU uninitialized", !sg_isvalid());
}
class ComputeBufferApp : public App {
    StorageBuffer buffer_;
    ComputeShader compute_;
    BufferReader reader_;
    Fbo target_;
    std::array<ShaderVertex, 24> vertices_ = {};
    void drawBuffer(float offset) {
        reader_.setUniform(UB_draw_params, Vec4(offset, 0, 0, 0));
        reader_.begin();
        reader_.submitVertices(vertices_.data(), int(vertices_.size()), PrimitiveType::Triangles);
        reader_.end();
    }
    uint32_t setupFrame_ = 0;
    void expectBytes(const unsigned char* pixels, int width, int height, const char* label,
                     bool prefixGreen, bool suffixGreen, bool firstOnly, bool checkAlpha) {
        bool matches = pixels != nullptr;
        for (int y = 0; y < height && matches; ++y) for (int x = 0; x < width; ++x) {
            const auto* pixel = &pixels[(y * width + x) * 4];
            const int cell = (2 * x + 1) * 4 / width;
            const bool green = (cell < 4 ? prefixGreen : suffixGreen) && (!firstOnly || cell % 4 == 0);
            matches &= pixel[0] == (green ? 0 : 255) && pixel[1] == (green ? 255 : 0) &&
                pixel[2] == ((cell % 2) ? 255 : 0) && (!checkAlpha || pixel[3] == 255);
        }
        check(label, matches);
    }
    void expect(const char* label, bool prefixGreen = false, bool suffixGreen = false, bool firstOnly = false) {
        std::array<unsigned char, 32 * 4 * 4> pixels = {};
        check("buffer drawing readback succeeds", target_.readPixels(pixels.data()));
        expectBytes(pixels.data(), 32, 4, label, prefixGreen, suffixGreen, firstOnly, true);
    }

public:
    void setup() override {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        std::printf("GPU: %s / %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
#endif
        check("backend supports compute", sg_query_features().compute);
        if (!sg_query_features().compute) { exitApp(1); return; }
        std::array<Vec4, 4> initial = {Vec4(1,0,0,1), Vec4(1,0,1,1), Vec4(1,0,0,1), Vec4(1,0,1,1)};
        check("storage allocation with initial data succeeds", buffer_.allocate(sizeof(initial), initial.data()));
        const auto desc = sg_query_buffer_desc(buffer_.getBuffer());
        check("storage has persistent byte size and view", buffer_.getSize() == sizeof(initial) &&
            desc.size == sizeof(initial) && desc.usage.storage_buffer && !desc.usage.write_transient &&
            sg_query_view_buffer(buffer_.getView()).id == buffer_.getBuffer().id);
        check("compute descriptor loads", compute_.load(buffer_pattern_shader_desc));
        check("compute buffer binding succeeds", compute_.setStorageBuffer(VIEW_pattern_output, buffer_));
        check("graphics descriptor loads", reader_.load(buffer_reader_shader_desc));
        check("graphics buffer binding succeeds", reader_.setStorageBuffer(VIEW_pattern_input, buffer_));
        check("negative compute slot is refused", !compute_.setStorageBuffer(-1, buffer_));
        check("undeclared compute slot is refused", !compute_.setStorageBuffer(1, buffer_));
        check("graphics slot limit is refused", !reader_.setStorageBuffer(SG_MAX_VIEW_BINDSLOTS, buffer_));
        check("undeclared graphics slot is refused", !reader_.setStorageBuffer(1, buffer_));
        target_.allocate(32, 4);
        target_.begin();
        drawBuffer(0); drawBuffer(1);
        target_.end();
        expect("initial CPU data reaches graphics");
        initial[0] = Vec4(0, 1, 0, 1);
        check("first CPU upload succeeds", buffer_.update(initial.data(), sizeof(Vec4)));
        setupFrame_ = sg_query_stats().prev_frame.frame_index;
        target_.begin();
        drawBuffer(0); drawBuffer(1);
        target_.end();
        expect("partial CPU upload reaches graphics and preserves remaining bytes", true, true, true);
        check("second CPU upload in same device frame is refused", !buffer_.update(initial.data(), sizeof(initial)));
        check("null CPU upload is refused", !buffer_.update(nullptr, 16));
        check("oversized CPU upload is refused", !buffer_.update(initial.data(), sizeof(initial) + 16));
        check("unaligned CPU upload is refused", !buffer_.update(initial.data(), 3));
    }
    void draw() override {
        auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level >= LogLevel::Error) { ++failures; std::printf("GPU ERROR %s\n", event.message.c_str()); }
        });
        // setup() and the first draw may share a device frame.
        if (setupFrame_ == sg_query_stats().prev_frame.frame_index) return;
        std::array<Vec4, 4> initial = {Vec4(1,0,0,1), Vec4(1,0,1,1), Vec4(1,0,0,1), Vec4(1,0,1,1)};
        check("CPU upload in a later device frame succeeds", buffer_.update(initial.data(), sizeof(initial)));
        target_.begin();
        drawBuffer(0);
        compute_.setUniform(UB_buffer_params, Vec4(0, 1, 0, 0));
        check("compute writes known pattern mid-frame", compute_.dispatch(1));
        drawBuffer(1);
        target_.end();
        expect("graphics prefix sees CPU data and suffix sees compute output", false, true);
        compute_.setUniform(UB_buffer_params, Vec4(1, 0, 0, 0));
        check("restore buffer before window prefix", compute_.dispatch(1));
        drawBuffer(0);
        check("window buffer prefix is lazy", !internal::currentTarget().inPass);
        compute_.setUniform(UB_buffer_params, Vec4(0, 1, 0, 0));
        check("dispatch flushes recorded window buffer drawing", compute_.dispatch(1));
        drawBuffer(1);
        internal::ensureCurrentPass();
        internal::parkWindowPass();
        Pixels screen;
        check("window buffer readback succeeds", grabScreen(screen));
        expectBytes(screen.getData(), screen.getWidth(), screen.getHeight(),
                    "window prefix sees old buffer and suffix sees compute output", false, true, false, false);

        // Deferred recordings keep handles alive when the owner is cleared.
        target_.begin();
        compute_.setUniform(UB_buffer_params, Vec4(1, 0, 0, 0));
        check("repeat dispatch in same device frame succeeds", compute_.dispatch(1));
        drawBuffer(0); drawBuffer(1);
        StorageBuffer moved(std::move(buffer_));
        check("move transfers buffer, view and size", moved.isAllocated() && moved.getSize() == sizeof(initial) &&
            !buffer_.isAllocated() && !buffer_.getBuffer().id && !buffer_.getView().id && !buffer_.getSize());
        moved.clear();
        check("clear resets storage", !moved.isAllocated() && !moved.getSize() && !moved.getBuffer().id && !moved.getView().id);
        target_.end();
        expect("cleared owner retains resources for recorded graphics");
        check("binding rejects cleared storage", !compute_.setStorageBuffer(0, moved));
        StorageBuffer empty;
        check("zero allocation is refused", !empty.allocate(0));
        check("unaligned allocation is refused", !empty.allocate(3));
        check("allocation without initial data succeeds", empty.allocate(64));
        StorageBuffer assigned;
        assigned.allocate(16);
        assigned = std::move(empty);
        check("move assignment transfers storage", assigned.isAllocated() && assigned.getSize() == 64 && !empty.isAllocated());
        completed = true;
        exitApp();
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64).setHighDpi(false).setSampleCount(1);
        return tcCoreTest::runDisplayTest<ComputeBufferApp>(settings, failures, [] { check("GPU checks completed", completed); });
    }
    headlessChecks();
    return failures ? 1 : 0;
}
