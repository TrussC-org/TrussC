// #821, PR 0. Frame-indexed scenes: no elapsed-time inputs or external assets.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "regression.glsl.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#if defined(SOKOL_GLCORE)
#include <GL/gl.h>
#elif defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#elif defined(SOKOL_D3D11)
#include <d3d11.h>
#include <dxgi.h>
#elif defined(SOKOL_METAL)
#include <objc/message.h>
#include <objc/runtime.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <X11/Xlib.h>
#endif

using namespace tc;
namespace {
using Json = nlohmann::json;
using Rgba = std::array<int, 4>;
struct Scene { const char* name; int tolerance; };
// Baseline comparison tolerances (maximum difference in any RGBA8 channel).
// Only rasterized 3D/MSAA scenes have a nonzero allowance; never auto-adjust.
constexpr Scene scenes[] = {
    {"blendModes", 0}, {"text", 0}, {"premultImage", 0},
    {"fboClearLoad", 0}, {"msaaResolve", 2}, {"transformPbr", 2},
    {"easyCamFbo", 2}, {"scissorFbo", 0}, {"fullscreenShader", 0},
    {"pbrShadowFbo", 2}, {"pointMeshTransform", 2},
    {"resizePixelPerfect", 0}, {"userShaderOtherFormat", 0},
    {"readbackRoundTrip", 0}, {"midFrameFlush", 0}, {"midPassClear", 0}
};
int failures = 0;
bool completed = false;
std::string mode;
fs::path baseline, output;

void check(const std::string& label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
std::string gpuName() {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    const auto* text = glGetString(GL_RENDERER);
    return text ? reinterpret_cast<const char*>(text) : "unavailable";
#elif defined(SOKOL_D3D11)
    auto* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC desc = {};
    std::string result = "unavailable";
    if (device && SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi)))) {
        if (SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                char utf8[512] = {};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, utf8, sizeof(utf8), nullptr, nullptr);
                result = utf8;
            }
            adapter->Release();
        }
        dxgi->Release();
    }
    return result;
#elif defined(SOKOL_METAL)
    // Objective-C runtime calls keep this combined-test source ordinary C++.
    auto sendObject = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend);
    auto sendText = reinterpret_cast<const char* (*)(id, SEL)>(objc_msgSend);
    id device = reinterpret_cast<id>(const_cast<void*>(sg_mtl_device()));
    id name = sendObject(device, sel_registerName("name"));
    const char* text = sendText(name, sel_registerName("UTF8String"));
    return text ? text : "unavailable";
#else
    return "unsupported";
#endif
}
Rgba pixel(const Pixels& pixels, int x, int y) {
    const Color c = pixels.getColor(x, y);
    return {int(std::lround(c.r * 255)), int(std::lround(c.g * 255)),
            int(std::lround(c.b * 255)), int(std::lround(c.a * 255))};
}

class RegressionApp : public App {
    Fbo target_;
    Texture premult_, coverage_;
    Mesh triangle_, points_, receiver_;
    sg_image resumeDepth_ = {};
    sg_view resumeDepthView_ = {};
    Material emissive_;
    Light light_;
    EasyCam camera_;
    Shader user_;
    FullscreenShader fullscreen_, discard_;
    EventListener after_;
    Json samples_ = Json::object();
    size_t scene_ = 0;
    int phase_ = 0, variant_ = 0, settleFrames_ = 0;
    int logicalW_ = 256, logicalH_ = 192;
    std::set<std::string> captured_;
    std::string gpu_;

    const std::string name() const { return scenes[scene_].name; }
    bool is(const char* text) const { return name() == text; }
    float screenScale() const { return internal::pixelPerfectMode() ? 1.0f : getDpiScale(); }
    void rect(float x, float y, float w, float h, Color color) {
        setColor(color); drawRect(x, y, w, h);
    }
    void pattern(float w, float h) {
        rect(0, 0, w/2, h/2, Color(1, 0, 0));
        rect(w/2, 0, w/2, h/2, Color(0, 1, 0));
        rect(0, h/2, w/2, h/2, Color(0, 0, 1));
        rect(w/2, h/2, w/2, h/2, Color(1, 1, 0));
    }
    void pbr(float x, float y) {
        setMaterial(emissive_);
        pushMatrix(); translate(x, y); rotate(0.19f); scale(1.1f, 0.9f);
        triangle_.draw(); popMatrix(); clearMaterial();
    }
    void prepare() {
        phase_ = 0; settleFrames_ = 0;
        const bool resize = is("resizePixelPerfect");
        // Four captures in this scene: both sizes with both coordinate modes.
        logicalW_ = resize && variant_ >= 2 ? 320 : 256;
        logicalH_ = resize && variant_ >= 2 ? 224 : 192;
        internal::pixelPerfectMode() = resize && (variant_ % 2 == 1);
        if (getFramebufferWidth() != int(logicalW_*getDpiScale()) ||
            getFramebufferHeight() != int(logicalH_*getDpiScale())) {
#if defined(__linux__) && !defined(__ANDROID__)
            // Main-window setWindowSizeLogical is still a Linux stub. Drive
            // its native X11 window in the harness, without changing that API.
            auto* display = static_cast<Display*>(const_cast<void*>(sapp_x11_get_display()));
            const auto window = static_cast<::Window>(reinterpret_cast<uintptr_t>(sapp_x11_get_window()));
            XResizeWindow(display,window,unsigned(logicalW_*getDpiScale()),unsigned(logicalH_*getDpiScale()));
            XSync(display,False);
#else
            setWindowSizeLogical(logicalW_, logicalH_);
#endif
        }
    }
    void allocateTarget() {
        int w = getFramebufferWidth(), h = getFramebufferHeight();
        if (is("easyCamFbo")) { w = 160; h = 96; }
        if (is("scissorFbo")) { w = 320; h = 224; }
        const int count = is("msaaResolve") ? 4 : 1;
        const auto format = is("userShaderOtherFormat") ? TextureFormat::RGBA16F : TextureFormat::RGBA8;
        if (target_.getWidth() != w || target_.getHeight() != h ||
            target_.getSampleCount() != count || target_.getTextureFormat() != format)
            target_.allocate(w, h, count, format);
        if (is("pbrShadowFbo") || is("midFrameFlush")) {
            if (resumeDepth_.id == 0) {
                sg_image_desc desc = {};
                desc.usage.depth_stencil_attachment = true;
                desc.width = w; desc.height = h; desc.sample_count = 1;
                desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
                resumeDepth_ = sg_make_image(&desc);
                sg_view_desc view = {}; view.depth_stencil_attachment.image = resumeDepth_;
                resumeDepthView_ = sg_make_view(&view);
            }
        }
    }
    void shadowPass(bool offscreen) {
        // Test-only bridge for the renderer before PR 3: keep the Fbo active
        // while suspending its raw pass around the nested shadow pass. The
        // public shadow API currently suspends only swapchains. No production
        // target-stack behavior is changed in PR 0.
        if (offscreen) {
            internal::flushLayersMidPass();
            sg_end_pass();
        }
        beginShadowPass(light_);
        pushMatrix(); translate(128,96,24); shadowDraw(triangle_); popMatrix();
        endShadowPass();
        if (offscreen) {
            sg_pass pass = {};
            pass.attachments.colors[0] = target_.getTexture().getAttachmentView();
            pass.attachments.depth_stencil = resumeDepthView_;
            pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
            pass.action.depth.load_action = SG_LOADACTION_CLEAR;
            pass.action.depth.clear_value = 1;
            sg_begin_pass(&pass);
        }
    }
    void drawScene(float w, float h, bool offscreen) {
        if (is("blendModes")) {
            constexpr BlendMode blends[] = {BlendMode::Alpha, BlendMode::Add, BlendMode::Multiply,
                BlendMode::Screen, BlendMode::Subtract, BlendMode::Disabled};
            for (int i = 0; i < 6; ++i) {
                setBlendMode(BlendMode::Disabled);
                rect(8 + i*40, 16, 32, 64, Color(0.25f, 0.5f, 0.75f));
                setBlendMode(blends[i]);
                rect(8 + i*40, 16, 32, 32, Color(1, 0.25f, 0, 0.5f));
                setColor(0.25f,0.75f,1,0.5f);
                drawCircle(24+i*40,108,14);
                drawTriangle(10+i*40,160,38+i*40,160,24+i*40,130);
                drawLine(10+i*40,176,38+i*40,168);
            }
            setBlendMode(BlendMode::Alpha);
        } else if (is("text")) {
            setColor(1, 1, 1); setTextAlign(Direction::Left, Direction::Top);
            drawBitmapString("Render 821\nAa 0123", 16, 16);
            // The embedded bitmap glyph atlas is also rendered through the R8
            // coverage pipeline used by Font; no platform font substitution.
            internal::loadPipeline(internal::activeCoverage2D());
            sgl_enable_texture(); sgl_texture(coverage_.getView(), coverage_.getSampler());
            sgl_begin_quads();
            sgl_v2f_t2f(16, 64, 0, 0); sgl_v2f_t2f(32, 64, 1, 0);
            sgl_v2f_t2f(32, 96, 1, 1); sgl_v2f_t2f(16, 96, 0, 1);
            sgl_end(); sgl_disable_texture(); internal::restoreCurrentPipeline();
        } else if (is("premultImage")) {
            rect(0, 0, w, h, Color(0, 0, 1));
            setColor(1, 1, 1); premult_.draw(16, 16, 64, 64);
        } else if (is("fboClearLoad")) {
            rect(16, 16, 48, 48, Color(1, 0, 0, 0.5f));
        } else if (is("msaaResolve")) {
            rect(16, 16, 48, 48, Color(1, 0, 0));
            setColor(0, 1, 0); drawTriangle(96, 16, 155, 77, 83, 81);
        } else if (is("transformPbr")) {
            pushMatrix(); translate(24, 16); rotate(0.2f); scale(1.2f, 0.8f);
            rect(0, 0, 48, 48, Color(0, 1, 0)); pbr(100, 64); popMatrix();
        } else if (is("easyCamFbo")) {
            camera_.begin(); pbr(0, 0); camera_.end();
        } else if (is("scissorFbo")) {
            const float dpi = offscreen ? 1.0f : screenScale();
            pushScissor(16*dpi, 16*dpi, 32*dpi, 32*dpi);
            rect(0, 0, 96, 96, Color(1, 0, 0)); popScissor();
            rect(w-32, h-32, 16, 16, Color(0, 1, 0));
        } else if (is("fullscreenShader")) {
            fullscreen_.draw();
        } else if (is("pbrShadowFbo")) {
            light_.setDiffuse(1,1,1); light_.setDirectional(0.6f,0,-1);
            shadowPass(offscreen);
            Material surface; surface.setBaseColor(0.5f,0.5f,0.5f).setRoughness(1);
            setMaterial(surface);
            pushMatrix(); translate(128,96,0); receiver_.draw(); popMatrix(); clearMaterial();
            light_.setDiffuse(0,0,0);
            pbr(w/2, h/2);
            light_.setDiffuse(0,0,0); light_.setDirectional(0,0,-1);
        } else if (is("pointMeshTransform")) {
            setPointStyle(PointStyle::Square); setPointSize(12);
            pushMatrix(); translate(48, 32); rotate(0.2f); scale(1.3f);
            points_.draw(); popMatrix();
        } else if (is("resizePixelPerfect") || is("readbackRoundTrip")) {
            pattern(w, h);
            if (is("resizePixelPerfect")) {
                rect(w-2, 0, 1, h, Color(1, 0, 1));
                rect(0, h-2, w, 1, Color(0, 1, 1));
            }
        } else if (is("userShaderOtherFormat")) {
            const ShaderVertex quad[] = {{-0.75f,-0.75f,0,0,0,0,1,0,1}, {0.75f,-0.75f,0,0,0,0,1,0,1},
                {0.75f,0.75f,0,0,0,0,1,0,1}, {-0.75f,0.75f,0,0,0,0,1,0,1}};
            user_.begin(); user_.submitVertices(quad, 4, PrimitiveType::Quads); user_.end();
        } else if (is("midPassClear")) {
            // Exercise clear() in an active pass on both targets, including
            // commands queued since the last flush. They must never replay.
            discard_.draw();
            rect(16, 16, 32, 32, Color(1, 0, 0, 0.5f));
            rect(64, 16, 32, 32, Color(1, 0, 0));
            clear(0, 0, 1, 1);
            rect(112, 16, 32, 32, Color(0, 1, 0));
        } else if (is("midFrameFlush")) {
            rect(16, 16, 32, 32, Color(1, 0, 0, 0.5f));
            pbr(140,156); // deferred layer 0, followed by nonzero sgl layers
            const float x0 = 2*16/w-1, x1 = 2*48/w-1;
            const float y0 = 1-2*144/h, y1 = 1-2*168/h;
            const ShaderVertex quad[] = {{x0,y0,0,0,0,0,1,0,1}, {x1,y0,0,0,0,0,1,0,1},
                {x1,y1,0,0,0,0,1,0,1}, {x0,y1,0,0,0,0,1,0,1}};
            user_.begin(); user_.submitVertices(quad,4,PrimitiveType::Quads); user_.end();
            // Exercise the FullscreenShader flush and the shadow-pass flush
            // independently: probes before, between and after the two sites.
            discard_.draw();
            rect(64, 16, 32, 32, Color(1, 0, 0, 0.5f));
            shadowPass(offscreen);
            pbr(160, 110);
            rect(112, 16, 32, 32, Color(1, 0, 0, 0.5f));
        }
    }
    Rgba at(const Pixels& px, float x, float y, float scaleFactor) const {
        return pixel(px, int(x * scaleFactor), int(y * scaleFactor));
    }
    void expect(const Pixels& px, bool offscreen, const std::string& label, float x, float y,
                Rgba expected, float scaleFactor, int allowance = 1) {
        const auto got = at(px, x, y, scaleFactor);
        int difference = 0;
        // Swapchain alpha varies by backend/format; only Fbo probes assert alpha.
        for (int c = 0; c < (offscreen ? 4 : 3); ++c) difference = std::max(difference, std::abs(got[c]-expected[c]));
        std::printf("  %s (%g,%g) rgba=[%d,%d,%d,%d] expected=[%d,%d,%d,%d]\n", label.c_str(), x, y,
            got[0], got[1], got[2], got[3], expected[0], expected[1], expected[2], expected[3]);
        check(label, difference <= allowance);
    }
    void assertions(const Pixels& px, bool offscreen) {
        const std::string label = name() + (offscreen ? ".fbo" : ".screen");
        const float s = offscreen ? 1 : screenScale();
        const float w = px.getWidth()/s, h = px.getHeight()/s;
        if (is("resizePixelPerfect")) {
            check(label+" framebuffer size", px.getWidth() == int(logicalW_*getDpiScale()) &&
                px.getHeight() == int(logicalH_*getDpiScale()));
            expect(px, offscreen, label+" pixel column", w-1.5f, h/4, {255,0,255,255}, s);
            expect(px, offscreen, label+" pixel row", w/4, h-1.5f, {0,255,255,255}, s);
        }
        if (is("blendModes")) {
            constexpr Rgba expected[] = {{160,96,95,255}, {192,160,191,255},
                {64,80,95,255}, {160,144,191,255}, {0,96,191,255}, {255,64,0,128}};
            for (int i=0;i<6;++i)
                expect(px, offscreen,label+" blend "+std::to_string(i),24.0f+i*40,32,expected[i],s);
            expect(px, offscreen, label+" background strip", 24, 64, {64,128,191,255}, s);
        } else if (is("text")) {
            // R glyph's vertical stem in the embedded bitmap font.
            expect(px, offscreen, label+" glyph", 18, 20, {255,255,255,255}, s);
            expect(px, offscreen, label+" R8 coverage glyph", 21, 73, {255,255,255,255}, s);
        } else if (is("premultImage")) {
            // Known wrong: Fbo uses Fill2D instead of Premult; #821 PR 4 pipelines fixes it.
            expect(px, offscreen, label+" premult", 32, 32, offscreen ? Rgba{64,0,127,255} : Rgba{128,0,127,255}, s);
        } else if (is("fboClearLoad")) {
            Rgba red{128,0,0,128}, green{0,255,0,255};
            int allowance = 1;
#if defined(SOKOL_METAL)
            // Known wrong on Metal only: Air captures BOTH screen probes as
            // [0,0,0,0], while the Fbo is exact. This scene never restarts a
            // swapchain pass: the Fbo CLEAR/LOAD precedes its first present().
            // Capture already uses that pass's drawable and ordered queue.
            // Tracked in #832 (Fbo sampling/presentation vs capture on Metal);
            // no proven #821 PR 3/5 fix yet. Keep exact current RGB, not a tolerance.
            if (!offscreen) { red = green = Rgba{0,0,0,0}; allowance = 0; }
#endif
            expect(px, offscreen, label+" loaded red", 32, 32, red, s, allowance);
            expect(px, offscreen, label+" second pass", 96, 32, green, s, allowance);
        } else if (is("msaaResolve") || is("scissorFbo")) {
            // Known wrong in scissorFbo: popScissor clips the larger Fbo to window size; #821 PR 2 matrices fixes the captured lower-right patch.
            expect(px, offscreen, label+" red interior", 32, 32, {255,0,0,255}, s);
            expect(px, offscreen, label+" exterior", 8, 8, {0,0,0,255}, s);
        } else if (is("transformPbr")) {
            expect(px, offscreen, label+" transformed primitive", 40, 32, {0,255,0,255}, s);
            // Known wrong: outer model leaks into Fbo; #821 PR 2 matrices fixes 149,99 vs 132,90.
            expect(px, offscreen, label+" transformed PBR", offscreen ? 149.0f : 132.0f,
                offscreen ? 99.0f : 90.0f, {232,0,0,255}, s, 2);
        } else if (is("easyCamFbo")) {
            // Known wrong: Fbo camera uses window aspect/size; #821 PR 2 matrices fixes the captured extent.
            expect(px, offscreen, label+" PBR emissive", w/2, h/2, {232,0,0,255}, s, 2);
        } else if (is("fullscreenShader")) {
            expect(px, offscreen, label+" upper left", w/4, h/4, {255,255,0,255}, s);
            expect(px, offscreen, label+" lower right", w*3/4, h*3/4, {0,0,0,255}, s);
        } else if (is("pbrShadowFbo")) {
            expect(px, offscreen, label+" PBR", w/2, h/2, {231,0,0,255}, s, 2);
            expect(px, offscreen,label+" lit receiver",80,80,{128,128,128,255},s,2);
            expect(px, offscreen,label+" shadow receiver",140,104,{42,42,42,255},s,2);
        } else if (is("pointMeshTransform")) {
            // Known wrong: #655 ignores model transforms; #821 PR 4 pipelines fixes this origin probe.
            expect(px, offscreen, label+" GPU point", 24, 24, {0,255,0,255}, s);
        } else if (is("resizePixelPerfect") || is("readbackRoundTrip")) {
            expect(px, offscreen, label+" top left", w/4, h/4, {255,0,0,255}, s);
            expect(px, offscreen, label+" top right", w*3/4, h/4, {0,255,0,255}, s);
            expect(px, offscreen, label+" bottom left", w/4, h*3/4, {0,0,255,255}, s);
            expect(px, offscreen, label+" bottom right", w*3/4, h*3/4, {255,255,0,255}, s);
        } else if (is("userShaderOtherFormat")) {
            expect(px, offscreen, label+" user shader", w/2, h/2, {0,255,0,255}, s);
        } else if (is("midPassClear")) {
            expect(px, offscreen,label+" erased alpha",32,32,{0,0,255,255},s,0);
            expect(px, offscreen,label+" erased opaque",80,32,{0,0,255,255},s,0);
            expect(px, offscreen,label+" post-clear",128,32,{0,255,0,255},s,0);
        } else if (is("midFrameFlush")) {
            for (int x : {32,80,128}) expect(px, offscreen, label+" ONE blend", float(x), 32, {128,0,0,255}, s);
            expect(px, offscreen,label+" pre-flush PBR layer",140,156,{231,0,0,255},s,2);
            expect(px, offscreen,label+" pre-flush user layer",32,156,{0,255,0,255},s);
        }
    }
    void capture(Pixels& px, const std::string& target, bool offscreen) {
        assertions(px, offscreen);
        std::string key = name();
        if (is("resizePixelPerfect")) key += "." + std::to_string(logicalW_) + "x" +
            std::to_string(logicalH_) + (internal::pixelPerfectMode() ? ".pixel" : ".logical");
        key += "." + target;
        captured_.insert(key);
        Json sampled = Json::array();
        for (auto xy : {std::array<int,2>{8,8}, {24,24}, {32,32}, {80,32}, {128,32}, {132,90}, {149,99}, {128,96}, {18,20}, {21,73},
                        {80,48}, {80,80}, {140,104}, {32,156}, {140,156}}) {
            const float s = offscreen ? 1 : screenScale();
            if (int(xy[0]*s) < px.getWidth() && int(xy[1]*s) < px.getHeight())
                sampled.push_back({{"xy", {int(xy[0]*s),int(xy[1]*s)}}, {"rgba", at(px,xy[0],xy[1],s)}});
        }
        samples_[key] = {{"size", {px.getWidth(), px.getHeight()}}, {"backend", GraphicsBackend::name()},
            {"gpu", gpu_}, {"tolerance", scenes[scene_].tolerance}, {"pixels", sampled}};
        if (mode == "--self-check") return;
        check(key+" saved", px.save(output / (key+".png")).ok());
        if (mode != "--compare") return;
        Pixels reference;
        const bool loaded = reference.load(baseline / (key+".png")).ok();
        const bool sameSize = loaded && reference.getWidth() == px.getWidth() && reference.getHeight() == px.getHeight();
        Pixels diff; diff.allocate(px.getWidth(), px.getHeight(), 4);
        size_t red = 0;
        for (int y = 0; y < px.getHeight(); ++y) for (int x = 0; x < px.getWidth(); ++x) {
            const auto actual = pixel(px,x,y);
            bool changed = !sameSize;
            if (sameSize) {
                const auto before = pixel(reference,x,y);
                for (int c = 0; c < 4; ++c) changed |= std::abs(before[c]-actual[c]) > scenes[scene_].tolerance;
            }
            auto* p = diff.getData() + (size_t(y)*px.getWidth()+x)*4;
            p[0] = changed ? 255 : actual[0]/4; p[1] = changed ? 0 : actual[1]/4;
            p[2] = changed ? 0 : actual[2]/4; p[3] = 255;
            red += changed;
        }
        const fs::path diffPath = output / (key+".diff.png");
        check(key+" diff saved", diff.save(diffPath).ok());
        std::printf("COMPARE %s: %zu pixels over tolerance %d%s; diff=%s\n", key.c_str(), red,
            scenes[scene_].tolerance, sameSize ? "" : " (missing/unreadable/size mismatch)", diffPath.string().c_str());
        check(key+" baseline", red == 0);
    }
    void afterFrame() {
        if (completed) return;
        if (phase_ == 0) {
            const int requestedW = int(logicalW_*getDpiScale());
            const int requestedH = int(logicalH_*getDpiScale());
            if (getFramebufferWidth() != requestedW || getFramebufferHeight() != requestedH) {
                if (++settleFrames_ >= 60) {
                    std::printf("FAIL %s resize did not settle after 60 frames: requested %dx%d, got %dx%d\n",
                        name().c_str(), requestedW, requestedH, getFramebufferWidth(), getFramebufferHeight());
                    ++failures; exitApp(1);
                }
                return;
            }
            phase_ = 1;
            return; // Render the settled size before capturing it.
        }
        if (++phase_ != 3) return;
        Pixels screen;
        if (grabScreen(screen) && screen.isAllocated()) capture(screen, "screen", false);
        else check(name()+" grabScreen", false);
        Pixels pixels; pixels.allocate(target_.getWidth(),target_.getHeight(),4);
        bool read = false;
        if (target_.getTextureFormat() == TextureFormat::RGBA16F) {
            std::vector<float> floats(size_t(pixels.getWidth())*pixels.getHeight()*4);
            read = target_.readPixelsFloat(floats.data());
            for (size_t i = 0; i < floats.size(); ++i)
                pixels.getData()[i] = static_cast<unsigned char>(std::lround(std::clamp(floats[i],0.0f,1.0f)*255));
        } else read = target_.readPixels(pixels.getData());
        if (read) capture(pixels, "fbo", true);
        else check(name()+" readPixels", false);
        if (is("readbackRoundTrip") && screen.isAllocated() && read) {
            // At DPI 2, screen uses logical w/h with a w*2/h*2 viewport;
            // Fbo uses framebuffer w/h with a matching projection/viewport.
            // pattern() divides each target into the same exact half rectangles,
            // so all pixels must match even before #821 PR 2 matrices (no model/camera).
            bool identical = screen.getWidth() == pixels.getWidth() && screen.getHeight() == pixels.getHeight();
            if (identical) for (size_t i = 0; i < size_t(pixels.getWidth())*pixels.getHeight(); ++i)
                for (int c = 0; c < 3; ++c)
                    identical &= screen.getData()[i*4+c] == pixels.getData()[i*4+c];
            check("grabScreen/readPixels identical RGB pattern", identical);
        }
        phase_ = 0;
        if (is("resizePixelPerfect") && ++variant_ < 4) { prepare(); return; }
        variant_ = 0;
        if (++scene_ < std::size(scenes)) { prepare(); return; }
        if (mode != "--self-check") {
            std::ofstream file(output/"samples.json"); file << samples_.dump(2) << '\n';
            check("samples.json written", file.good());
        }
        // Detect a scene removed from the head, too (not just a missing base).
        if (mode == "--compare") for (const auto& entry : fs::directory_iterator(baseline)) {
            const auto file = entry.path().filename();
            if (file.extension() == ".png" && !captured_.count(file.stem().string())) {
                Pixels reference;
                const bool loaded = reference.load(entry.path()).ok();
                Pixels missing;
                missing.allocate(loaded ? reference.getWidth() : 1, loaded ? reference.getHeight() : 1, 4);
                const size_t count = size_t(missing.getWidth())*missing.getHeight();
                for (size_t i=0;i<count;++i) {
                    auto* rgba = missing.getData()+i*4;
                    rgba[0]=255; rgba[1]=0; rgba[2]=0; rgba[3]=255;
                }
                const fs::path diffPath = output/(file.stem().string()+".diff.png");
                check("missing scene diff saved", missing.save(diffPath).ok());
                std::printf("COMPARE %s: %zu pixels over tolerance 0 (missing in head); diff=%s\n",
                    file.stem().string().c_str(),count,diffPath.string().c_str());
                check("unrendered baseline scene "+file.string(), false);
            }
        }
        completed = true; exitApp(failures ? 1 : 0);
    }
public:
    void setup() override {
        randomSeed(821); setDefaultScreenFov(0); gpu_ = gpuName();
        check("user shader loaded", user_.load(regression_mesh_shader_desc));
        check("fullscreen shader loaded", fullscreen_.load(regression_full_shader_desc));
        check("discard shader loaded", discard_.load(regression_discard_shader_desc));
        const unsigned char texel[] = {128,0,0,128};
        Pixels image; image.allocate(1,1,4); std::memcpy(image.getData(),texel,sizeof(texel));
        premult_.allocate(image); premult_.setPremultipliedAlpha(true);
        // Embedded default bitmap font, 'R', copied into an R8 coverage page.
        auto* atlas = bitmapfont::generateAtlasPixels(1);
        std::vector<unsigned char> glyph(8*16);
        // Atlas addressing uses the same cell layout as drawBitmapString.
        const int cell = int('R') - 0x20;
        for (int y=0;y<16;++y) for(int x=0;x<8;++x)
            glyph[y*8+x] = atlas[((cell/bitmapfont::CELLS_PER_ROW*bitmapfont::CELL_H+y)*bitmapfont::ATLAS_WIDTH+
                cell%bitmapfont::CELLS_PER_ROW*bitmapfont::CELL_W+x)*4+3];
        delete[] atlas;
        Pixels glyphPixels; glyphPixels.allocate(8,16,1);
        std::memcpy(glyphPixels.getData(),glyph.data(),glyph.size());
        coverage_.setFilter(TextureFilter::Nearest); coverage_.allocate(glyphPixels);
        triangle_.setMode(PrimitiveMode::Triangles);
        triangle_.addVertex(-24,-24,0).addVertex(24,-24,0).addVertex(0,24,0);
        for (int i=0;i<3;++i) triangle_.addNormal(0,0,1);
        points_.setMode(PrimitiveMode::Points); points_.addVertex(24,24,0).addColor(Color(0,1,0));
        points_.addVertex(80,64,0).addColor(Color(1,0,1));
        emissive_.setBaseColor(0,0,0).setEmissive(1,0,0).setEmissiveStrength(1);
        light_.setDirectional(0,0,-1); light_.setDiffuse(0,0,0); light_.setAmbient(0,0,0);
        light_.enableShadow(64); addLight(light_);
        receiver_ = createPlane(160,112);
        light_.setShadowArea(Vec3(128,96,0),128);
        camera_.setDistance(180);
        prepare();
        after_ = events().afterFrame.listen([this] { afterFrame(); });
    }
    void draw() override {
        if (completed) return;
        if (phase_ == 0) return; // All platforms wait for the requested framebuffer size.
        allocateTarget();
        setBlendMode(BlendMode::Alpha); fill(); resetScissor();
        clear(0,0,0,1);
        // Put an outer transform across begin() to capture the known FBO model
        // leakage, without changing the independent self-check location.
        const bool transform = is("transformPbr");
        if (transform) { pushMatrix(); translate(17, 9, 0); }
        const bool transparent = is("fboClearLoad");
        target_.begin(0,0,0,transparent ? 0.0f : 1.0f);
        drawScene(float(target_.getWidth()),float(target_.getHeight()),true);
        target_.end();
        if (transform) popMatrix();
        if (transparent) {
            target_.begin(); rect(80,16,32,48,Color(0,1,0)); target_.end();
            // Copy the loaded Fbo without blending; screen probes check RGB only.
            setBlendMode(BlendMode::Disabled); setColor(1,1,1);
            target_.draw(0,0,getWindowWidth(),getWindowHeight()); setBlendMode(BlendMode::Alpha);
        } else {
            // pixelPerfect is toggled internally between same-size captures;
            // App's RectNode size changes only on a native resize event. Query
            // the current coordinate size (framebuffer / screenScale) instead.
            drawScene(float(getWindowWidth()),float(getWindowHeight()),false);
        }
    }
    void exit() override {
        clearLights();
        internal::deferGpuDestroy(resumeDepthView_); internal::deferGpuDestroy(resumeDepth_);
        // AppKit terminates inside runApp(), so the verdict must be set during
        // the lifecycle. This also makes an early native-window close fail.
        check("all "+std::to_string(std::size(scenes))+" scenes completed", completed);
        exitApp(failures ? 1 : 0);
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 1) {
        std::printf("SKIP: renderRegression needs a GPU/window; use --self-check, --record <dir>, or --compare <dir>\n");
        return 0;
    }
    mode = argv[1];
    if (!((mode == "--self-check" && argc == 2) ||
          ((mode == "--record" || mode == "--compare") && argc == 3))) {
        std::fprintf(stderr,"Usage: renderRegression [--self-check | --record <dir> | --compare <dir>]\n"); return 2;
    }
#if defined(__EMSCRIPTEN__)
    std::printf("SKIP: renderRegression excludes Web\n"); return 0;
#else
    try {
        if (mode != "--self-check") {
            baseline = fs::absolute(argv[2]).lexically_normal();
            if (baseline.filename().empty()) baseline = baseline.parent_path();
            if (mode == "--compare" && !fs::is_directory(baseline)) {
                std::fprintf(stderr,"Baseline directory missing: %s\n",baseline.string().c_str()); return 1;
            }
            output = mode == "--record" ? baseline : fs::path(baseline.string()+".compare");
            // Refuse stale outputs: every successful recording is a full run.
            if (fs::exists(output) && !fs::is_empty(output)) {
                std::fprintf(stderr,"Output must be empty: %s\n",output.string().c_str()); return 2;
            }
            fs::create_directories(output);
        }
        WindowSettings settings;
        settings.setSize(256,192); settings.setSwapInterval(0); settings.sampleCount = 1;
        // The verdict leaves through the app (macOS never returns from runApp);
        // a startup failure keeps runApp's non-zero result.
        return tcCoreTest::runDisplayTest<RegressionApp>(settings, failures);
    } catch (const std::exception& e) { check(e.what(),false); }
    return failures ? 1 : 0;
#endif
}
