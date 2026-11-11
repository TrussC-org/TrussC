#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>

using namespace tc;
namespace {
// Count only this module's warnings; sokol may log unrelated ones (e.g. a
// one-time uniform buffer growth on Metal) during the same loop.
bool isStencilWarning(const LogEventArgs& e) {
    return e.level == LogLevel::Warning && e.message.rfind("[Stencil] ", 0) == 0;
}

int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("stencilOwnership %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
bool bytesEqual(const Fbo& fbo, uint8_t value) {
    Pixels pixels;
    if (!fbo.readStencil(pixels) || pixels.getChannels() != 1 ||
        pixels.getWidth() != fbo.getWidth() || pixels.getHeight() != fbo.getHeight()) return false;
    for (int i = 0; i < pixels.getWidth() * pixels.getHeight(); ++i)
        if (pixels.getData()[i] != value) return false;
    return true;
}
void allValues() {
    Fbo fbo;
    fbo.allocate(7, 5);
    check("new attachment starts at zero", bytesEqual(fbo, 0));
    fbo.begin();
    check("begin ownership", beginStencil());
    auto* target = &internal::currentTarget();
    fbo.end();
    bool exact = true, handback = true, refused = true;
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (isStencilWarning(e)) ++warnings;
    });
    for (int v = 0; v < 256; ++v) {
        fbo.begin();
        if (!target->stencilOwned) beginStencil();
        // Clears ignore transforms, viewport and scissor, even at value zero.
        setupScreenOrtho();
        translate(100, 100); scale(0.25f);
        target->viewport = {1, 1, 2, 2, true};
        pushScissor(2, 2, 1, 1);
        clearStencil(uint8_t(v));
        popScissor();
        fbo.end();
        exact &= bytesEqual(fbo, uint8_t(v));
        fbo.begin();
        // begin does not erase previously stored bytes; color clears preserve them.
        clear(0.25f, 0.5f, 0.75f);
        endStencil();
        fbo.end();
        handback &= bytesEqual(fbo, 0);
        fbo.begin();
        clearStencil(uint8_t(255 - v));
        clearStencil();
        fbo.end();
        refused &= bytesEqual(fbo, 0);
    }
    check("all 256 owned clears byte-exact over full attachment", exact);
    check("all 256 handbacks zero the whole stencil byte", handback);
    check("unowned clear leaves all bytes unchanged", refused);
    check("unowned clear warns on every call", warnings == 512);
    fbo.begin(); check("borrow existing bytes", beginStencil()); fbo.end();
    check("begin leaves existing bytes unchanged", bytesEqual(fbo, 0));
}
void nestingAndState() {
    Fbo a, b, c;
    a.allocate(9, 6); b.allocate(9, 6); c.allocate(9, 6);
    a.begin();
    auto& ta = internal::currentTarget();
    // Synthetic future framework state exercises semantic capture/restore
    // without exposing write/test APIs reserved for slice 2b.
    ta.stencil.raster.enabled = true;
    ta.stencil.raster.front.compare = SG_COMPAREFUNC_EQUAL;
    ta.stencil.raster.back.compare = SG_COMPAREFUNC_NOT_EQUAL;
    ta.stencil.raster.front.pass_op = SG_STENCILOP_INCR_CLAMP;
    ta.stencil.raster.ref = 3;
    ta.stencil.raster.read_mask = 7;
    ta.stencil.raster.write_mask = 0x80;
    ta.stencil.colorWriteMask = SG_COLORMASK_R;
    check("A begin", beginStencil());
    check("ownership disables framework overrides", !ta.stencil.raster.enabled &&
          ta.stencil.colorWriteMask == SG_COLORMASK_RGBA);
    clearStencil(255);
    b.begin();
    check("B does not inherit ownership", !internal::currentTarget().stencilOwned);
    check("B begin", beginStencil()); clearStencil(85);
    c.begin();
    check("C does not inherit nested ownership", !internal::currentTarget().stencilOwned);
    c.end();
    check("B restored after C", internal::currentTarget().stencilOwned);
    b.end();
    check("A restored after B", &internal::currentTarget() == &ta && ta.stencilOwned);
    resetStyle(); setBlendMode(BlendMode::Add); enableDepthTest(); disableDepthTest();
    check("style and depth preserve ownership", ta.stencilOwned);
    a.end();
    check("A and B independent bytes", bytesEqual(a, 255) && bytesEqual(b, 85));
    b.begin(1, 0, 0); clear(0, 1, 0); b.end();
    check("color begin and clear preserve stencil", bytesEqual(b, 85));
    a.begin(); endStencil();
    check("semantic snapshot restored", !ta.stencilOwned && ta.stencil.raster.enabled &&
          ta.stencil.raster.front.compare == SG_COMPAREFUNC_EQUAL &&
          ta.stencil.raster.back.compare == SG_COMPAREFUNC_NOT_EQUAL &&
          ta.stencil.raster.front.pass_op == SG_STENCILOP_INCR_CLAMP &&
          ta.stencil.raster.ref == 3 && ta.stencil.raster.read_mask == 7 &&
          ta.stencil.raster.write_mask == 0x80 && ta.stencil.colorWriteMask == SG_COLORMASK_R);
    a.end();
    b.begin(); endStencil(); b.end();
    check("independent handbacks", bytesEqual(a, 0) && bytesEqual(b, 0));
    // Moves carry semantic ownership with the attachment; reallocations reset it.
    a.begin(); check("begin before move", beginStencil()); clearStencil(231); a.end();
    Fbo moved(std::move(a));
    moved.begin(); check("move carries ownership", internal::currentTarget().stencilOwned); moved.end();
    check("move carries bytes", bytesEqual(moved, 231));
    moved.allocate(5, 3);
    check("reallocation initializes bytes", bytesEqual(moved, 0));
    moved.begin(); check("reallocation resets ownership", !internal::currentTarget().stencilOwned); moved.end();
}
void refusals() {
    Fbo fbo;
    fbo.allocate(4, 4);
    fbo.begin();
    auto& target = internal::currentTarget();
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (isStencilWarning(e)) ++warnings;
    });
    target.frameworkStencilClipDepth = 1;
    check("active framework clip refused", !beginStencil() && !target.stencilOwned);
    target.frameworkStencilClipDepth = 0;
    check("begin after clip", beginStencil()); clearStencil(129);
    check("already owned refused", !beginStencil() && target.stencilOwned);
    fbo.end();
    check("refused begin preserves bytes", bytesEqual(fbo, 129));
    fbo.begin(); endStencil(); endStencil(); fbo.end();
    Texture color;
    color.allocate(3, 2, TextureFormat::RGBA8, TextureUsage::RenderTarget);
    sg_pass pass = {};
    pass.attachments.colors[0] = color.getAttachmentView();
    {
        internal::TargetPassScope scope(pass, 3, 2, SG_PIXELFORMAT_RGBA8, 1, false);
        check("color-only pass active", scope.active());
        check("missing stencil attachment refused", !beginStencil() && !internal::currentTarget().stencilOwned);
        clearStencil(42);
    }
    check("refusals warn", warnings == 5);
}
void ordering() {
    Fbo fbo;
    fbo.allocate(8, 4);
    fbo.begin(0, 0, 0);
    setupScreenOrtho(); setBlendMode(BlendMode::Disabled);
    setColor(1, 0, 0); drawRect(0, 0, 8, 4);
    check("ordered begin", beginStencil());
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
    check("depth test begin", beginStencil());
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
    endStencil();
    quad(0.8f, 0, 1);
    fbo.end();
    Pixels pixels; pixels.allocate(4, 4, 4);
    bool exact = fbo.readPixels(pixels.getData());
    for (int i = 0; i < 16; ++i)
        exact &= pixels.getData()[i * 4] == 255 && pixels.getData()[i * 4 + 1] == 0;
    check("owned clear and handback preserve earlier depth", exact);
}
class OwnershipApp : public App {
    Fbo persistent;
    int frame = 0;
    void draw() override {
        if (frame == 0) {
            allValues(); nestingAndState(); refusals(); ordering(); depthPreservation();
            persistent.allocate(6, 4);
            persistent.begin(); check("persistent begin", beginStencil()); clearStencil(173); persistent.end();
        } else if (frame == 1) {
            check("bytes persist to next frame", bytesEqual(persistent, 173));
            persistent.begin(1, 0, 0);
            check("ownership persists to next frame", internal::currentTarget().stencilOwned);
            clear(0, 0, 1); persistent.end();
        } else {
            check("color clears preserve bytes across frames", bytesEqual(persistent, 173));
            persistent.begin(); endStencil(); persistent.end();
            check("handback across frames", bytesEqual(persistent, 0));
            completed = true; exitApp();
        }
        ++frame;
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    int warnings = 0;
    {
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (isStencilWarning(e)) ++warnings;
        });
        const bool first = beginStencil();
        const bool second = beginStencil();
        check("first targetless begin refused", !first);
        check("second targetless begin refused", !second);
    }
    check("targetless begin warns on every call", warnings == 2);
    if (argc != 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::puts("SKIP: stencilOwnership needs --gpu-check and a display");
        return failures ? 1 : 0;
    }
    WindowSettings settings;
    settings.setSize(64, 48).setHighDpi(false).setSampleCount(1).setSwapInterval(0);
    return tcCoreTest::runDisplayTest<OwnershipApp>(settings, failures, [] {
        check("test completed", completed);
    });
}
