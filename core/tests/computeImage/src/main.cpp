// #1281: storage output, sampled input, uniforms and render/compute ordering.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "pattern.compute.glsl.h"
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
constexpr int side = 9; // Partial workgroups exercise the kernel bounds check.
void check(const char* label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
void refusalChecks() {
    int warnings = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("ComputeShader") != std::string::npos) ++warnings;
    });
    ComputeShader dispatchOnly;
    check("headless dispatch fails without GPU initialization", !dispatchOnly.dispatch(1));
    ComputeShader loader;
    check("headless load fails", !loader.load(compute_pattern_shader_desc) && !loader.isLoaded());
    check("each object reports its refusal", warnings == 2);
    Texture texture;
    texture.allocate(side, side, TextureFormat::RGBA8, TextureUsage::Storage);
    check("headless storage texture stays unallocated", !texture.isAllocated() && !texture.getStorageView().id);
    check("refused compute leaves GPU uninitialized", !sg_isvalid());
}
class HeadlessComputeApp : public App {
    void setup() override { check("headless runtime active", headless::isActive()); refusalChecks(); }
    void update() override { exitApp(); }
};

class ComputeImageApp : public App {
public:
    void setup() override {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        std::printf("GPU: %s / %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
#endif
        check("backend supports compute", sg_query_features().compute);
        if (!sg_query_features().compute) { exitApp(1); return; }
        Pixels pixels;
        pixels.allocate(side, side, 4);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                pixels.setColor(x, y, Color(1, 1, ((x + y) % 2) ? 1.0f : 0.0f, 1));
        input_.allocate(pixels);
        input_.setFilter(TextureFilter::Nearest);
        output_.allocate(side, side, TextureFormat::RGBA8, TextureUsage::Storage);
        output_.setFilter(TextureFilter::Nearest);
        check("storage texture has separate sampled and storage views", output_.isAllocated() &&
              output_.getView().id && output_.getStorageView().id &&
              output_.getView().id != output_.getStorageView().id && !output_.getAttachmentView().id);
        check("compute descriptor loads", compute_.load(compute_pattern_shader_desc));
        check("storage binding accepts output", compute_.setStorageImage(VIEW_output_image, output_));
        check("sampled binding accepts input", compute_.setTexture(VIEW_source_image, input_));
        target_.allocate(side * 2, side);
        child_.allocate(side * 2, side);
        check("Fbo storage writes are refused", !compute_.setStorageImage(VIEW_output_image, target_.getTexture()));
        // Both tints preserve the blue checkerboard from the sampled input.
        compute_.setUniform(UB_params, Vec4(1, 0, 1, 1));
        check("setup dispatch writes initial pattern", compute_.dispatch(2, 2, 1));
    }

    void draw() override {
        auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level >= LogLevel::Error) { ++failures; std::printf("GPU ERROR %s\n", event.message.c_str()); }
        });
        // The window prefix is still lazy when the child starts its pass.
        setColor(1, 1, 1, 1);
        output_.draw(0, 0);
        check("window parent prefix is lazy", !internal::currentTarget().inPass);
        child_.begin();
        output_.draw(0, 0);
        compute_.setUniform(UB_params, Vec4(0, 1, 1, 1));
        check("dispatch inside child flushes window parent", compute_.dispatch(2, 2, 1));
        output_.draw(side, 0);
        child_.end();
        output_.draw(side, 0);
        internal::ensureCurrentPass();
        internal::parkWindowPass();
        Pixels parentScreen;
        check("window parent readback succeeds", grabScreen(parentScreen));
        expectBytes(parentScreen.getData(), "window parent prefix survives child dispatch", false, parentScreen.getWidth());
        expectPattern(child_, "child prefix follows window parent and precedes dispatch");

        // A dispatch parks the Fbo; its next recording becomes a lazy parent.
        target_.begin();
        compute_.setUniform(UB_params, Vec4(1, 0, 1, 1));
        check("dispatch parks Fbo parent", compute_.dispatch(2, 2, 1));
        output_.draw(0, 0);
        check("Fbo parent prefix is lazy", !internal::currentTarget().inPass);
        child_.begin();
        output_.draw(0, 0);
        compute_.setUniform(UB_params, Vec4(0, 1, 1, 1));
        check("dispatch inside child flushes Fbo parent", compute_.dispatch(2, 2, 1));
        output_.draw(side, 0);
        child_.end();
        output_.draw(side, 0);
        target_.end();
        expectPattern(target_, "Fbo parent prefix survives child dispatch");
        expectPattern(child_, "child prefix follows Fbo parent and precedes dispatch");

        compute_.setUniform(UB_params, Vec4(1, 0, 1, 1));
        check("restore initial pattern", compute_.dispatch(2, 2, 1));
        // Active Fbo path, including a LOAD continuation.
        target_.begin();
        clear(0, 0, 0, 1);
        setColor(1, 1, 1, 1);
        output_.draw(0, 0);
        compute_.setUniform(UB_params, Vec4(0, 1, 1, 1));
        check("mid-frame dispatch succeeds", compute_.dispatch(2, 2, 1));
        output_.draw(side, 0);
        target_.end();
        expectPattern(target_, "Fbo prefix sees old output; suffix sees compute output");

        target_.begin(1, 0, 0, 1);
        check("dispatch before Fbo clear succeeds", compute_.dispatch(2, 2, 1));
        clear(0, 0, 1, 1);
        output_.draw(side, 0);
        target_.end();
        std::array<unsigned char, side * side * 2 * 4> cleared = {};
        check("Fbo clear readback succeeds", target_.readPixels(cleared.data()));
        check("clear after dispatch erases earlier Fbo drawing", cleared[0] == 0 &&
              cleared[1] == 0 && cleared[2] == 255 && cleared[3] == 255);

        // Window draws can still be CPU recordings with no active pass.
        compute_.setUniform(UB_params, Vec4(1, 0, 1, 1));
        check("second dispatch succeeds in same device frame", compute_.dispatch(2, 2, 1));
        output_.draw(0, 0);
        check("window prefix is recorded lazily", !internal::currentTarget().inPass);
        compute_.setUniform(UB_params, Vec4(0, 1, 1, 1));
        check("dispatch flushes lazy window prefix", compute_.dispatch(2, 2, 1));
        output_.draw(side, 0);
        internal::ensureCurrentPass();
        internal::parkWindowPass();
        // Capture the window through the existing screenshot path.
        // Window alpha is backend-defined; the Fbo check covers all RGBA channels.
        Pixels screen;
        check("window readback succeeds", grabScreen(screen));
        expectBytes(screen.getData(), "window prefix sees old output; suffix sees compute output", false, screen.getWidth());

        Texture moved(std::move(output_));
        check("Texture move transfers storage view", moved.isAllocated() && moved.getStorageView().id && !output_.getStorageView().id);
        ComputeShader movedCompute(std::move(compute_));
        check("ComputeShader move transfers pipeline", movedCompute.isLoaded() && !compute_.isLoaded());
        check("zero groups are a successful no-op", movedCompute.dispatch(0));
        check("negative groups are refused", !movedCompute.dispatch(-1));
        check("oversized groups are refused", !movedCompute.dispatch(65536));
        movedCompute.clear();
        check("clear unloads compute", !movedCompute.isLoaded() && !movedCompute.dispatch(1));
        moved.clear();
        check("clear resets both views", !moved.getView().id && !moved.getStorageView().id);

        Texture rejected;
        rejected.allocate(side, side, TextureFormat::BGRA8, TextureUsage::Storage);
        check("unsupported storage format is refused", !rejected.isAllocated());
        rejected.allocate(side, side, TextureFormat::RGBA8, TextureUsage::Storage, 4);
        check("MSAA storage is refused", !rejected.isAllocated());
        rejected.allocate(side, side, TextureFormat::RGBA8, TextureUsage::Storage, 1, 2);
        check("storage mip chain is refused", !rejected.isAllocated());
        rejected.allocateCubemap(side, TextureFormat::RGBA8, TextureUsage::Storage);
        check("storage cubemap is refused", !rejected.isAllocated());
        for (auto format : {TextureFormat::RGBA8, TextureFormat::RGBA16F, TextureFormat::RGBA32F, TextureFormat::R32F}) {
            const auto caps = sg_query_pixelformat(toSokolFormat(format));
            const char* name = format == TextureFormat::RGBA8 ? "RGBA8" :
                format == TextureFormat::RGBA16F ? "RGBA16F" :
                format == TextureFormat::RGBA32F ? "RGBA32F" : "R32F";
            std::string warning;
            auto refusalLog = getLogger().onLog.listen([&](LogEventArgs& event) {
                if (event.level == LogLevel::Warning) warning += event.message;
            });
            Texture storage;
            storage.allocate(side, side, format, TextureUsage::Storage);
            std::printf("Storage %s: %s (write=%d sample=%d filter=%d)\n", name,
                storage.isAllocated() ? "accepted" : "refused", caps.write, caps.sample, caps.filter);
            check("Storage allocation matches format capabilities",
                  storage.isAllocated() == (caps.write && caps.sample && caps.filter));
            check("unfilterable storage warning names format and reason", caps.filter ||
                  (warning.find(name) != std::string::npos && warning.find("not filterable on this GPU") != std::string::npos));
            if (format == TextureFormat::RGBA8 || format == TextureFormat::RGBA16F)
                check("portable Storage format allocates", storage.isAllocated());
            if (format == TextureFormat::RGBA16F && storage.isAllocated()) {
                ComputeShader half;
                check("RGBA16F compute descriptor loads", half.load(compute_half_shader_desc));
                check("RGBA16F storage binding succeeds", half.setStorageImage(VIEW_half_output, storage));
                check("RGBA16F compute write succeeds", half.dispatch(2, 2, 1));
                target_.begin();
                storage.draw(0, 0, side * 2, side);
                target_.end();
                std::array<unsigned char, side * side * 2 * 4> bytes = {};
                check("RGBA16F drawing readback succeeds", target_.readPixels(bytes.data()));
                bool matches = true;
                for (size_t i = 0; i < bytes.size(); i += 4)
                    matches &= std::abs(int(bytes[i]) - 64) <= 1 && std::abs(int(bytes[i + 1]) - 128) <= 1 &&
                        std::abs(int(bytes[i + 2]) - 191) <= 1 && bytes[i + 3] == 255;
                check("RGBA16F compute output draws correct pixels", matches);
            }
        }
        completed = true;
        exitApp();
    }
private:
    void expectBytes(const unsigned char* bytes, const char* label, bool checkAlpha = true, int rowPixels = side * 2) {
        bool ok = bytes != nullptr;
        for (int y = 0; y < side && ok; ++y) {
            for (int x = 0; x < side * 2; ++x) {
                const auto* pixel = bytes + (y * rowPixels + x) * 4;
                const int expectedBlue = (((x % side) + y) % 2) ? 255 : 0;
                const bool matches = pixel[0] == (x < side ? 255 : 0) && pixel[1] == (x < side ? 0 : 255) &&
                      pixel[2] == expectedBlue && (!checkAlpha || pixel[3] == 255);
                if (ok && !matches) std::printf("%s: pixel (%d,%d) = %u,%u,%u,%u, expected %d,%d,%d,255\n",
                    label, x, y, pixel[0], pixel[1], pixel[2], pixel[3], x < side ? 255 : 0, x < side ? 0 : 255, expectedBlue);
                ok &= matches;
            }
        }
        check(label, ok);
    }
    void expectPattern(Fbo& fbo, const char* label) {
        std::array<unsigned char, side * side * 2 * 4> bytes = {};
        check("Fbo readPixels succeeds", fbo.readPixels(bytes.data()));
        expectBytes(bytes.data(), label);
    }
    Texture input_, output_;
    ComputeShader compute_;
    Fbo target_, child_;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        settings.setSampleCount(1);
        return tcCoreTest::runDisplayTest<ComputeImageApp>(settings, failures, [] { check("GPU checks completed", completed); });
    }
    refusalChecks();
    runHeadlessApp<HeadlessComputeApp>();
    return failures ? 1 : 0;
}
