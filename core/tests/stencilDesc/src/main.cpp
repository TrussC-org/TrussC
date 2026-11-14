#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "pipeline.glsl.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace tc;
int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
bool bytesEqual(const Fbo& fbo, uint8_t value) {
    Pixels pixels;
    if (!fbo.readStencil(pixels)) return false;
    for (int i = 0; i < pixels.getWidth() * pixels.getHeight(); ++i)
        if (pixels.getData()[i] != value) return false;
    return true;
}
Pixels readColor(const Fbo& fbo) {
    Pixels pixels;
    pixels.allocate(fbo.getWidth(), fbo.getHeight(), 4);
    check("color readback", fbo.readPixels(pixels.getData()));
    return pixels;
}
// Only this compatibility test calls the deprecated wrappers.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
void compatibilityWrappers() {
    Fbo fbo;
    fbo.allocate(4, 4);
    fbo.begin();
    clearStencil(73);
    setStencilDesc(StencilDesc::inside(73));
    check("begin wrapper reports attachment and preserves desc", beginStencil() &&
        getStencilDesc() == StencilDesc::inside(73));
    fbo.end();
    check("begin wrapper preserves stored bytes", bytesEqual(fbo, 73));
    fbo.begin();
    endStencil();
    check("end wrapper resets desc", getStencilDesc() == StencilDesc{});
    auto& target = internal::currentTarget();
    const auto depthFormat = target.depthFormat;
    target.depthFormat = SG_PIXELFORMAT_NONE;
    check("begin wrapper reports missing attachment", !beginStencil());
    target.depthFormat = depthFormat;
    fbo.end();
    check("end wrapper clears stored bytes", bytesEqual(fbo, 0));
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
void clearValues() {
    Fbo fbo;
    fbo.allocate(7, 5);
    check("new attachment starts at zero", bytesEqual(fbo, 0));
    bool exact = true;
    for (int value = 0; value < 256; ++value) {
        fbo.begin();
        setupScreenOrtho();
        translate(100, 100);
        scale(0.25f);
        internal::currentTarget().viewport = {1, 1, 2, 2, true};
        pushScissor(2, 2, 1, 1);
        // Neither the descriptor's masks nor scissor/transform limit clearing.
        setStencilDesc(StencilDesc::shape(3, 1));
        clearStencil(uint8_t(value));
        resetStencilDesc();
        popScissor();
        fbo.end();
        exact &= bytesEqual(fbo, uint8_t(value));
    }
    check("all 256 clears byte-exact across full attachment without beginStencil", exact);
    fbo.begin(1, 0, 0);
    clear(0, 1, 0);
    fbo.end();
    check("color clears preserve stencil", bytesEqual(fbo, 255));
}
void cycling() {
    Fbo fbo;
    fbo.allocate(8, 4, 1, TextureFormat::RGBA16F);
    fbo.begin(0, 0, 0);
    setupScreenOrtho();
    setBlendMode(BlendMode::Add);
    bool exact = true;
    for (int round = 0; round < 2; ++round) {
        for (int value = 1; value <= 255; ++value) {
            auto desc = StencilDesc::drawOnce();
            desc.value = uint8_t(value);
            setStencilDesc(desc);
            setColor(round == 0 ? 1.0f : 0.0f, round == 1 ? 1.0f : 0.0f, 0, 1.0f / 1024);
            drawRect(0, 0, 8, 4);
            drawRect(0, 0, 8, 4);
        }
        // The benchmark pattern: recycle values only after clearing all bits.
        clearStencil();
    }
    resetStencilDesc();
    fbo.end();
    // Binary fractions remain exact in half floats and never saturate: a
    // broken stencil that contributes twice must produce twice the expected sum.
    Pixels pixels;
    pixels.allocate(8, 4, 4, PixelFormat::F32);
    exact &= fbo.readPixelsFloat(pixels.getDataF32());
    for (int i = 0; i < 32; ++i) {
        const auto* p = pixels.getDataF32() + i * 4;
        exact &= p[0] == 255.0f / 1024 && p[1] == 255.0f / 1024 && p[2] == 0;
    }
    check("draw-once cycles 1..255 twice with one color contribution per stroke", exact);
    check("clear every 255 strokes resets all bytes", bytesEqual(fbo, 0));
}
void shapeAndStyle() {
    Fbo actual, reference;
    actual.allocate(8, 4);
    reference.allocate(8, 4);
    actual.begin(0, 0, 0);
    setupScreenOrtho();
    clearStencil(0xA0);
    setStencilDesc(StencilDesc::shape(3, 0x0F));
    drawRect(0, 0, 4, 4);
    setStencilDesc(StencilDesc::inside(0xF3, 0x0F));
    setBlendMode(BlendMode::Disabled);
    setColor(1, 0, 0);
    drawRect(0, 0, 8, 4);
    pushStyle();
    setStencilDesc(StencilDesc::outside(3, 0x0F));
    setColor(0, 1, 0);
    drawRect(0, 0, 8, 4);
    setStencilDesc(StencilDesc::shape(99, 0x80));
    popStyle();
    check("push/pop restores all five descriptor fields", getStencilDesc() == StencilDesc::inside(0xF3, 0x0F));
    check("push/pop restores packed selection", internal::currentTarget().stencilDescKey == internal::stencilDescKey(getStencilDesc()));
    setColor(0, 0, 1);
    drawRect(0, 0, 8, 4);
    resetStencilDesc();
    check("resetStencilDesc restores default without resetting color", getStencilDesc() == StencilDesc{} && getColor() == Color(0, 0, 1));
    setStencilDesc(StencilDesc::shape());
    resetStyle();
    check("resetStyle restores default descriptor", getStencilDesc() == StencilDesc{});
    actual.end();
    Pixels stencil;
    bool mask = actual.readStencil(stencil);
    for (int y = 0; y < 4 && mask; ++y)
        for (int x = 0; x < 8; ++x)
            mask &= stencil.getData()[y * 8 + x] == (x < 4 ? 0xA3 : 0xA0);
    check("mask controls read and write while retaining unmasked bits", mask);
    reference.begin(0, 0, 0);
    setupScreenOrtho();
    setBlendMode(BlendMode::Disabled);
    setColor(0, 0, 1);
    drawRect(0, 0, 4, 4);
    setColor(0, 1, 0);
    drawRect(4, 0, 4, 4);
    reference.end();
    const auto a = readColor(actual);
    const auto b = readColor(reference);
    check("shape/inside/outside matches ordinary rectangle reference pixel for pixel", std::memcmp(a.getData(), b.getData(), 8 * 4 * 4) == 0);
    resetStyle();
}
struct Child : Node {
    void draw() override {
        check("child starts with default descriptor", getStencilDesc() == StencilDesc{});
        setBlendMode(BlendMode::Disabled);
        setColor(0, 1, 0);
        drawRect(0, 0, 8, 4);
    }
};
struct Parent : Node {
    Fbo fbo;
    void beginDraw() override {
        fbo.begin(0, 0, 0);
        setupScreenOrtho();
        setStencilDesc(StencilDesc::outside());
    }
    void endDraw() override;
    void draw() override {
        check("parent starts with default descriptor", getStencilDesc() == StencilDesc{});
        setStencilDesc(StencilDesc::shape(7));
        drawRect(0, 0, 4, 4);
        setStencilDesc(StencilDesc::inside(7));
    }
};
void Parent::endDraw() {
    check("node exit resets descriptor", getStencilDesc() == StencilDesc{});
    fbo.end();
    const auto pixels = readColor(fbo);
    bool green = true;
    for (int i = 0; i < 32; ++i) green &= pixels.getData()[i * 4 + 1] == 255;
    check("parent inside does not clip child drawing", green);
    Pixels stencil;
    bool retained = fbo.readStencil(stencil);
    for (int y = 0; y < 4 && retained; ++y)
        for (int x = 0; x < 8; ++x)
            retained &= stencil.getData()[y * 8 + x] == (x < 4 ? 7 : 0);
    check("node resets preserve stored stencil bytes", retained);
}
void missingAttachment() {
    Texture color;
    color.allocate(3, 2, TextureFormat::RGBA8, TextureUsage::RenderTarget);
    sg_pass pass = {};
    pass.attachments.colors[0] = color.getAttachmentView();
    internal::RenderTarget target;
    target.passKind = internal::TargetPassKind::Fixed;
    target.isFbo = true;
    target.width = 3;
    target.height = 2;
    target.colorFormat = SG_PIXELFORMAT_RGBA8;
    target.sampleCount = 1;
    target.hasDepth = false;
    target.depthFormat = SG_PIXELFORMAT_NONE;
    target.binding = pass;
    // Fixed raw scopes normally need no sgl context. This fixture records 2D draws.
    target.pooledResources = internal::leaseTargetResources(SG_PIXELFORMAT_RGBA8, 1, SG_PIXELFORMAT_NONE);
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.rfind("[Stencil] ", 0) == 0) ++warnings;
    });
    {
        internal::TargetPassScope scope(target);
        check("color-only pass active", scope.active());
        setupScreenOrtho();
        for (int i = 0; i < 3; ++i) {
            setStencilDesc(StencilDesc::shape());
            clearStencil(42);
            setColor(0, 1, 0);
            drawRect(0, 0, 3, 2);
        }
        check("missing attachment uses zero effective key", internal::currentTarget().stencilDescKey == 0);
        pushStyle();
        resetStencilDesc();
        popStyle();
        check("style restores requested desc without an attachment", getStencilDesc() == StencilDesc::shape());
        resetStyle();
        check("style reset clears requested desc without an attachment", getStencilDesc() == StencilDesc{} &&
            internal::currentTarget().requestedStencilDescKey == 0);
    }
    check("missing attachment warns once per target per operation", warnings == 2);
    Fbo readback;
    readback.allocate(3, 2);
    readback.begin();
    setupScreenOrtho();
    setColor(1);
    color.draw(0, 0);
    readback.end();
    const auto pixels = readColor(readback);
    bool green = pixels.getWidth() == 3 && pixels.getHeight() == 2;
    for (int i = 0; i < 6 && green; ++i) green &= pixels.getData()[i * 4 + 1] == 255;
    check("non-default descriptor without stencil draws with default color writes", green);
}
void ordering() {
    Fbo fbo;
    fbo.allocate(8, 4);
    fbo.begin(0, 0, 0);
    setupScreenOrtho(); setBlendMode(BlendMode::Disabled);
    setColor(1, 0, 0); drawRect(0, 0, 8, 4);
    internal::currentTarget().viewport = {2, 0, 4, 4, true};
    pushScissor(0, 0, 4, 4);
    const auto selection = internal::currentTarget().selectedPipeline;
    clearStencil(213);
    check("selected pipeline preserved", internal::currentTarget().selectedPipeline == selection);
    setColor(0, 1, 0); drawRect(0, 0, 8, 4);
    popScissor(); fbo.end();
    check("ordered clear full stencil", bytesEqual(fbo, 213));
    Pixels color; color.allocate(8, 4, 4);
    bool exact = fbo.readPixels(color.getData());
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) {
            const auto* p = color.getData() + (y * 8 + x) * 4;
            const bool green = x >= 2 && x < 4;
            exact &= p[0] == (green ? 0 : 255) && p[1] == (green ? 255 : 0) && p[2] == 0 && p[3] == 255;
        }
    check("clear preserves pending color and restores viewport, scissor and pipeline", exact);
}
void depthPreservation() {
    Fbo fbo;
    fbo.allocate(4, 4);
    fbo.begin(0, 0, 0);
    auto quad = [](float z, float r, float g) {
        auto key = internal::PipelineKey::immediate(internal::fillSelection(BlendMode::Disabled, true));
        internal::loadTemporaryPipeline(internal::currentTarget().pipeline(key).immediate);
        sgl_matrix_mode_projection(); sgl_push_matrix(); sgl_load_identity();
        sgl_matrix_mode_modelview(); sgl_push_matrix(); sgl_load_identity();
        sgl_begin_quads(); sgl_c4f(r, g, 0, 1);
        sgl_v3f(-1, -1, z); sgl_v3f(1, -1, z);
        sgl_v3f(1, 1, z); sgl_v3f(-1, 1, z); sgl_end();
        sgl_pop_matrix(); sgl_matrix_mode_projection(); sgl_pop_matrix();
        sgl_matrix_mode_modelview(); internal::restoreCurrentPipeline();
    };
    quad(0.2f, 1, 0);
    clearStencil(255);
    quad(0.8f, 0, 1);
    resetStencilDesc();
    quad(0.8f, 0, 1);
    fbo.end();
    Pixels pixels; pixels.allocate(4, 4, 4);
    bool exact = fbo.readPixels(pixels.getData());
    for (int i = 0; i < 16; ++i)
        exact &= pixels.getData()[i * 4] == 255 && pixels.getData()[i * 4 + 1] == 0;
    check("stencil clear and descriptor reset preserve earlier depth", exact);
}
void drawPaths() {
    Shader shader;
    check("custom shader loads", shader.load(stencil_desc_mesh_shader_desc));
    Font font;
    check("test font loads", font.load(fs::path(__FILE__).parent_path() / "../../renderTargetState/src/fixture.ttf", 16).ok());
    GlyphBatch glyphs;
    glyphs.setFont(font);
    glyphs.add("AA", Color(0, 1, 0));
    Mesh mesh;
    mesh.addVertex(0, 0).addVertex(32, 0).addVertex(0, 32);
    for (int i = 0; i < 3; ++i) mesh.addNormal(0, 0, 1);
    Material material;
    material.setEmissive(0, 1, 0);
    Texture texture;
    const unsigned char texel[] = {0, 255, 0, 255};
    texture.allocate(1, 1, TextureFormat::RGBA8, TextureUsage::Dynamic);
    texture.loadData(texel, 1, 1, 4);
    const ShaderVertex vertices[] = {
        {-1, -1, 0, 0, 0, 0, 1, 0, 1},
        {1, -1, 0, 0, 0, 0, 1, 0, 1},
        {-1, 1, 0, 0, 0, 0, 1, 0, 1},
        {1, 1, 0, 0, 0, 0, 1, 0, 1}};
    const char* names[] = {"shape", "texture", "Mesh", "custom Shader", "PBR", "square points", "round points", "pixel points", "Font", "GlyphBatch", "bitmap text"};
    for (int path = 0; path < int(std::size(names)); ++path) {
        Fbo reference, masked;
        reference.allocate(32, 32);
        masked.allocate(32, 32);
        for (auto* fbo : {&reference, &masked}) {
            fbo->begin(0, 0, 0);
            setupScreenOrtho();
            resetStyle();
            disableDepthTest();
            clearStencil();
            if (fbo == &masked) {
                setStencilDesc(StencilDesc::shape());
                drawRect(0, 0, 16, 32);
                setStencilDesc(StencilDesc::inside());
            }
            setColor(0, 1, 0);
            switch (path) {
                case 0: drawRect(0, 0, 32, 32); break;
                case 1: setColor(1); texture.draw(0, 0, 32, 32); break;
                case 2: mesh.draw(); break;
                case 3:
                    shader.begin();
                    shader.submitVertices(vertices, 4, PrimitiveType::TriangleStrip);
                    shader.end();
                    break;
                case 4:
                    setMaterial(material);
                    mesh.draw();
                    clearMaterial();
                    break;
                case 5: case 6: case 7:
                    setPointStyle(path == 5 ? PointStyle::Square : path == 6 ? PointStyle::Round : PointStyle::Pixel);
                    setPointSize(24);
                    drawPoint(16, 16);
                    break;
                case 8: font.drawString("AA", 4, 4); break;
                case 9: glyphs.draw(Mat4::translate(4, 4, 0)); break;
                case 10: drawBitmapString("AA", 4, 4); break;
            }
            resetStencilDesc();
            fbo->end();
        }
        const auto a = readColor(reference);
        const auto b = readColor(masked);
        bool exact = true, ink = false;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            const auto* r = a.getData() + (y * 32 + x) * 4;
            const auto* m = b.getData() + (y * 32 + x) * 4;
            ink |= r[0] || r[1] || r[2];
            for (int channel = 0; channel < 3; ++channel)
                exact &= m[channel] == (x < 16 ? r[channel] : 0);
        }
        std::printf("PATH %s\n", names[path]);
        check("half-mask matches default reference pixels", exact && ink);
    }
    resetStyle();
}
class StencilApp : public App {
    Fbo persistent;
    int frame = 0;
    void draw() override {
        if (frame++ == 0) {
            compatibilityWrappers();
            clearValues();
            ordering();
            depthPreservation();
            cycling();
            shapeAndStyle();
            drawPaths();
            auto parent = std::make_shared<Parent>();
            parent->fbo.allocate(8, 4);
            parent->addChild(std::make_shared<Child>());
            addChild(parent);
            missingAttachment();
            persistent.allocate(4, 4);
            persistent.begin();
            clearStencil(173);
            persistent.end();
        } else {
            removeAllChildren();
            check("bytes persist across frames", bytesEqual(persistent, 173));
            completed = true;
            exitApp();
        }
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    static_assert(internal::stencilDescKey(StencilDesc{}) == 0);
    static_assert(StencilDesc::drawOnce() == StencilDesc{1, StencilTest::NotEqual, true});
    static_assert(StencilDesc::shape() == StencilDesc{1, StencilTest::Always, true, 255, false});
    static_assert(StencilDesc::inside() == StencilDesc{1, StencilTest::Equal});
    static_assert(StencilDesc::outside() == StencilDesc{1, StencilTest::NotEqual});
    if (argc != 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::puts("SKIP: stencilDesc needs --gpu-check and a display");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(64, 48).setHighDpi(false).setSampleCount(1).setSwapInterval(0).setPipelinePoolSize(8192);
    return tcCoreTest::runDisplayTest<StencilApp>(settings, failures, [] {
        check("test completed", completed);
    });
}
