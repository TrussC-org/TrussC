#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

using namespace tc;
namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("perWindowShadows %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void layoutChecks() {
    Light light;
    light.enableShadow();
    check("public shadow default remains 1024", light.getShadowResolution() == 1024);
    size_t color, depth, total;
    check("default image payload is 20 MiB", internal::shadowImageLayout(1024,color,depth,total) &&
        color == 16777216 && depth == 4194304 && total == 20971520);
    check("invalid extents rejected", !internal::shadowImageLayout(0,color,depth,total) &&
        !internal::shadowImageLayout(-1,color,depth,total));
    check("byte product overflow rejected without allocation", !internal::shadowImageLayout(INT_MAX,color,depth,total));
    internal::WindowContext unused;
    check("unused window has no shadow generation", !unused.shadow.storage);
}
#if (defined(SOKOL_GLCORE) && defined(__linux__)) || (defined(SOKOL_D3D11) && defined(_WIN32)) || defined(SOKOL_METAL)
class ShadowApp : public App {
    internal::WindowContext* a_ = nullptr;
    std::shared_ptr<Window> b_, unused_;
    Light lights_[2];
    Mesh caster_, receiver_;
    Material material_;
    Fbo fbo_;
    EventListener draw_, log_;
    int unexpectedErrors_ = 0;
    bool ran_ = false;
    int capturePair_ = 0;
    int closeWait_ = 0;
    sg_image closingImage_{};
    std::weak_ptr<internal::ShadowStorage> closing_;
#if defined(SOKOL_D3D11) && defined(_WIN32)
    std::optional<internal::EntryStackGuard> secondaryDraw_;
#endif

    internal::WindowContext& owner(int n) { return n ? b_->context() : *a_; }
    void select(int n) {
#if defined(SOKOL_D3D11) && defined(_WIN32)
        secondaryDraw_.reset();
#endif
        internal::restoreWindowContext(&owner(n));
#if defined(SOKOL_D3D11) && defined(_WIN32)
        // Like captureSubmission, drive B's frame in a Draw entry. D3D11's
        // persistent backbuffer views also work outside B's native tick.
        if (n) secondaryDraw_.emplace(internal::AppEntry::Draw);
#endif
    }
    void start(int n, int resolution) {
        select(n);
        internal::prepareWindowTarget();
        beginFrame();
        if (owner(n).isMain) ++internal::updateFrameCount;
        else ++owner(n).updateCount;
        clear(0,0,0,1);
        clearLights();
        lights_[n].enableShadow(resolution);
        addLight(lights_[n]);
    }
    void cast(int n, bool second = false) {
        pushMatrix();
        translate((n ? 88 : 42) + (second ? 22 : 0), second ? 65 : 37, 24);
        shadowDraw(caster_);
        popMatrix();
    }
    void shadow(int n, bool two = false) {
        beginShadowPass(lights_[n]);
        cast(n);
        if (two) cast(n,true);
        endShadowPass();
        // Keep the following PBR CPU-only even if a prior pass resumed.
        internal::parkWindowPass();
    }
    void queue(bool fbo = false) {
        if (fbo) fbo_.begin(0,0,0,1);
        setMaterial(material_);
        pushMatrix(); translate(64,48,0); receiver_.draw(); popMatrix();
        clearMaterial();
    }
    Pixels finish(bool fbo = false) {
        Pixels pixels;
        if (fbo) {
            fbo_.end();
            pixels.allocate(128,96,4);
            check("Fbo readback", fbo_.readPixels(pixels.getData()));
#if defined(SOKOL_METAL)
            // Submit/retire Fbo work without presenting the main drawable
            // repeatedly inside its one native tick.
            internal::endGpuFrame();
#else
            present();
#endif
        } else {
            present();
            check("screen readback", captureWindow(pixels));
        }
        return pixels;
    }
    Pixels isolated(int n, int res, bool fbo = false, bool two = false, bool occluded = true) {
        start(n,res);
        beginShadowPass(lights_[n]);
        if (occluded) { cast(n); if (two) cast(n,true); }
        endShadowPass();
        internal::parkWindowPass();
        queue(fbo);
        return finish(fbo);
    }
    static size_t different(const Pixels& x, const Pixels& y, int tolerance = 2) {
        if (!x.isAllocated() || !y.isAllocated() || x.getWidth() != y.getWidth() ||
            x.getHeight() != y.getHeight() || x.getChannels() != y.getChannels()) return SIZE_MAX;
        size_t count = 0;
        const size_t pixels = size_t(x.getWidth()) * x.getHeight();
        for (size_t p = 0; p < pixels; ++p) {
            bool mismatch = false;
            for (int c = 0; c < x.getChannels(); ++c)
                mismatch |= std::abs(int(x.getData()[p*x.getChannels()+c]) - int(y.getData()[p*x.getChannels()+c])) > tolerance;
            count += mismatch;
        }
        return count;
    }
    void matches(const char* label, const Pixels& actual, const Pixels& reference) {
        const auto count = different(actual,reference);
        std::printf("perWindowShadows %s pixels outside tolerance 2: %zu\n",label,count);
        check(label,count == 0);
        if (const char* directory = std::getenv("TC_SHADOW_OUTPUT")) {
            fs::create_directories(directory);
            const auto prefix = fs::path(directory) / std::to_string(capturePair_++);
            check("actual image saved", actual.save(prefix.string()+"-actual.png").ok());
            check("reference image saved", reference.save(prefix.string()+"-reference.png").ok());
        }
    }
    void reportBytes() {
        for (int n : {0,1}) {
            const auto& s = *owner(n).shadow.storage;
            const auto color = sg_query_image_desc(s.colorImage);
            const auto depth = sg_query_image_desc(s.depthImage);
            const size_t colorBytes = size_t(color.width)*color.height*color.num_slices*4;
            const size_t depthBytes = size_t(depth.width)*depth.height*4;
            check("owner image descriptors match accounted bytes", color.pixel_format == SG_PIXELFORMAT_R32F &&
                depth.pixel_format == SG_PIXELFORMAT_DEPTH && color.num_slices == internal::maxShadowLights &&
                colorBytes+depthBytes == s.imageBytes);
            std::printf("perWindowShadows owner=%c resolution=%d colorBytes=%zu depthBytes=%zu imageBytes=%zu\n",
                'A'+n,s.resolution,colorBytes,depthBytes,s.imageBytes);
        }
    }
    void pair(int first, int resA, int resB, bool fbo = false) {
        const int second = 1-first;
        const int sizes[] = {resA,resB};
        auto refA = isolated(first,sizes[first],fbo);
        auto refB = isolated(second,sizes[second]);
        start(first,sizes[first]); shadow(first); queue(fbo);
        auto& target = internal::currentTarget();
        check("first PBR is deferred", target.deferredPbrDraws.size() == 1);
        const auto gen = owner(first).shadow.storage;
        check("PBR pins target owner's generation", target.deferredPbrDraws.back().cmd.shadowGeneration == gen &&
            target.deferredPbrDraws.back().cmd.bind.views[VIEW_tc_pbr_shadowMap].id == gen->colorTexture.id);
        start(second,sizes[second]); shadow(second); queue();
        const auto other = owner(second).shadow.storage;
        check("owners have distinct images/views/samplers", gen != other &&
            gen->colorImage.id != other->colorImage.id && gen->colorTexture.id != other->colorTexture.id &&
            gen->depthImage.id != other->depthImage.id && gen->sampler.id != other->sampler.id);
        auto actualB = finish();
        check("foreign commit preserves lazy PBR or its consumed Fbo output",
            target.deferredPbrDraws.size() == (fbo ? 0u : 1u));
        select(first);
        auto actualA = finish(fbo);
        matches("first owner matches isolated reference",actualA,refA);
        select(second);
        matches("second owner matches isolated reference",actualB,refB);
    }
    void fboAfterForeign(int first) {
        const int second = 1-first;
        const int sizes[] = {64,128};
        auto reference = isolated(first,sizes[first],true);
        start(first,sizes[first]); shadow(first);
        start(second,sizes[second]); shadow(second); queue(); finish();
        select(first);
        queue(true);
        matches("Fbo PBR after foreign shadow writes uses its owner maps",finish(true),reference);
    }
#if defined(SOKOL_METAL)
    void runMainCases() {
        if (ran_) return;
        ran_ = true;
        std::puts("SKIP: perWindowShadows secondary-window cases on Metal: the Air's second window is occluded / not reliably presented in the lab; interleaving, foreign commits, parked foreign shadow passes and native secondary close require it");

        // No second native window/drawable is needed for lazy allocation.
        internal::WindowContext unused;
        check("unused window context allocates zero shadow bytes", !unused.shadow.storage);
        start(0,1024);
        queue(true);
        check("main Fbo PBR without shadows has no shadow generation", !a_->shadow.storage &&
            !internal::currentTarget().deferredPbrDraws.back().cmd.shadowGeneration);
        finish(true);

        auto reference = isolated(0,1024,true);
        check("main shadow default remains 1024", a_->shadow.storage && a_->shadow.storage->resolution == 1024);
        matches("single-owner Fbo PBR matches isolated reference",isolated(0,1024,true),reference);
        a_->shadow.storage.reset(); a_->shadow.count = 0;
        auto oldReference = isolated(0,64,true);
        auto withoutShadow = isolated(0,64,true,false,false);
        const auto visible = different(oldReference,withoutShadow);
        check("reference contains a visible cast shadow", visible > 40 && visible != SIZE_MAX);

        start(0,64); shadow(0); queue(true);
        auto& pending = internal::currentTarget().deferredPbrDraws;
        std::weak_ptr<internal::ShadowStorage> old = a_->shadow.storage;
        const auto oldImage = a_->shadow.storage->colorImage;
        const auto oldView = a_->shadow.storage->colorTexture;
        check("main Fbo PBR is deferred and pins its owner generation", pending.size() == 1 &&
            pending.back().cmd.shadowGeneration == old.lock() &&
            pending.back().cmd.bind.views[VIEW_tc_pbr_shadowMap].id == oldView.id);
        check("growth preserves older Fbo recording", internal::ensureShadowStorage(*a_,256) &&
            a_->shadow.storage->resolution == 256 && a_->shadow.count == 0 &&
            pending.size() == 1 && pending.back().cmd.shadowGeneration == old.lock() &&
            !old.expired() && sg_query_image_state(oldImage) == SG_RESOURCESTATE_VALID &&
            sg_query_view_state(oldView) == SG_RESOURCESTATE_VALID);
        matches("older Fbo recording renders after growth",finish(true),oldReference);
        check("old generation retires after consumer commit", old.expired() &&
            sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID && sg_query_view_state(oldView) == SG_RESOURCESTATE_INVALID);
        auto grownReference = isolated(0,256,true);
        matches("grown Fbo PBR matches isolated reference",isolated(0,256,true),grownReference);
        check("unused window context still allocates zero shadow bytes", !unused.shadow.storage);
        check("no unexpected rendering errors", unexpectedErrors_ == 0);
        completed = true;
        exitApp();
    }
#else
    void runCases() {
        if (ran_) {
            if (b_->isOpen() && ++closeWait_ < 60) return;
            check("requested owner closed at native safe point", !b_->isOpen());
            internal::endGpuFrame();
            check("close retires pending owner maps", closing_.expired() &&
                !b_->context().shadow.storage && sg_query_image_state(closingImage_) == SG_RESOURCESTATE_INVALID);
            check("closing B preserves A maps", a_->shadow.storage &&
                sg_query_image_state(a_->shadow.storage->colorImage) == SG_RESOURCESTATE_VALID);
            check("no unexpected rendering errors", unexpectedErrors_ == 0);
            completed = true;
            exitApp();
            return;
        }
        ran_ = true;
        // PBR without a shadow pass must not allocate an owner's map set.
        internal::restoreWindowContext(&unused_->context());
#if defined(SOKOL_D3D11) && defined(_WIN32)
        {
        internal::EntryStackGuard unusedDraw(internal::AppEntry::Draw);
#endif
        internal::prepareWindowTarget(); beginFrame(); clear(0,0,0,1); queue();
        check("PBR without shadows has no shadow generation", !unused_->context().shadow.storage &&
            !internal::currentTarget().deferredPbrDraws.back().cmd.shadowGeneration);
        present();
#if defined(SOKOL_D3D11) && defined(_WIN32)
        }
#endif
        // Both owners initially use the public default; unused_ never shadows.
        pair(0,1024,1024);
        check("default unchanged for both windows", a_->shadow.storage->resolution == 1024 &&
            b_->context().shadow.storage->resolution == 1024);
        reportBytes();
        check("real unused window allocates zero shadow bytes", !unused_->context().shadow.storage);
        // Release the default sets before exercising smaller, distinct sizes.
        a_->shadow.storage.reset(); b_->context().shadow.storage.reset();
        a_->shadow.count = b_->context().shadow.count = 0;
        pair(1,64,128);
        reportBytes();
        pair(0,64,128,true);
        pair(1,64,128,true);
        fboAfterForeign(0);
        fboAfterForeign(1);
        auto withShadow = isolated(0,64);
        auto withoutShadow = isolated(0,64,false,false,false);
        check("reference contains a visible cast shadow", different(withShadow,withoutShadow) > 40 &&
            different(withShadow,withoutShadow) != SIZE_MAX);
        auto refB = isolated(1,128);
        check("A and B have deliberately different shadow pixels", different(withShadow,refB) > 40);

        auto oldReference = isolated(0,64);
        start(0,64); shadow(0); queue();
        auto& pending = a_->swapchainTarget.deferredPbrDraws;
        std::weak_ptr<internal::ShadowStorage> old = a_->shadow.storage;
        const auto oldImage = a_->shadow.storage->colorImage;
        const auto oldView = a_->shadow.storage->colorTexture;
        check("growth publishes a complete new generation", internal::ensureShadowStorage(*a_,256) &&
            a_->shadow.storage->resolution == 256 && a_->shadow.count == 0 &&
            pending.size() == 1 && pending.back().cmd.shadowGeneration == old.lock());
        start(1,128); shadow(1); queue(); finish();
        check("old generation survives foreign retirement", !old.expired() &&
            sg_query_image_state(oldImage) == SG_RESOURCESTATE_VALID && sg_query_view_state(oldView) == SG_RESOURCESTATE_VALID);
        select(0);
        matches("older recording renders after growth",finish(),oldReference);
        check("old generation retires after consumer commit", old.expired() &&
            sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID && sg_query_view_state(oldView) == SG_RESOURCESTATE_INVALID);
        pair(0,256,128);
        reportBytes();

        auto nestedReference = isolated(0,256,false,true);
        start(0,256); beginShadowPass(lights_[0]); cast(0);
        const auto attachment = a_->shadow.target.binding.attachments.colors[0];
        const auto token = a_->shadow.target.activation;
        start(1,128); shadow(1); queue(); finish();
        select(0);
        check("foreign tick parks an owner-local open shadow pass", a_->shadow.inPass &&
            !a_->shadow.target.inPass && a_->shadow.target.activation == token && a_->shadow.current == 0);
        cast(0,true);
        check("shadow pass resumes same attachment", a_->shadow.target.inPass &&
            a_->shadow.target.binding.attachments.colors[0].id == attachment.id);
        endShadowPass(); internal::parkWindowPass(); queue();
        matches("nested open shadow pass matches isolated reference",finish(),nestedReference);

        // Rewrite slot 0 on a later owner frame while earlier PBR is lazy.
        // Entering an Fbo must first consume that parent before the nested
        // shadow pass can clear its slot. The screen retains the earlier map.
        auto sameOwnerReference = isolated(0,256);
        for (bool child : {false,true}) {
            start(0,256); shadow(0); queue();
            check("same-owner prior PBR starts lazy", !a_->swapchainTarget.inPass);
            ++internal::updateFrameCount;
            if (child) fbo_.begin(0,0,0,1);
            beginShadowPass(lights_[0]); cast(1); endShadowPass();
            if (child) fbo_.end();
            check("target-stack entry consumed prior PBR", a_->swapchainTarget.deferredPbrDraws.empty());
            matches("same-owner rewrite preserves earlier screen pixels",finish(),sameOwnerReference);
        }

        start(1,128); shadow(1); queue();
        closingImage_ = b_->context().shadow.storage->colorImage;
        closing_ = b_->context().shadow.storage;
        select(0);
        b_->close(); // Verify actual native teardown on the next main tick.
    }
#endif
public:
    void setup() override {
        a_ = &internal::currentWindowContext();
        log_ = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level >= LogLevel::Error) ++unexpectedErrors_;
        });
#if defined(SOKOL_METAL)
        check("main window starts without shadow maps", !a_->shadow.storage);
#else
        WindowSettings settings; settings.setSize(128,96); settings.setHighDpi(false); settings.sampleCount = 1;
        b_ = createWindow(settings); unused_ = createWindow(settings);
        check("secondary and unused windows created", b_ && unused_);
        check("all windows start without shadow maps", !a_->shadow.storage &&
            b_ && !b_->context().shadow.storage && unused_ && !unused_->context().shadow.storage);
#if defined(SOKOL_D3D11) && defined(_WIN32)
        for (const auto& window : {b_,unused_}) {
            if (!window) continue;
            auto& ctx = window->context();
            const auto sc = ctx.acquireSwapchain(ctx.acquireSwapchainUser);
            check("D3D11 secondary views available before native tick", sc.d3d11.render_view &&
                sc.d3d11.depth_stencil_view && !sc.d3d11.resolve_view &&
                sc.width == 128 && sc.height == 96 && sc.sample_count == 1 &&
                sc.color_format == SG_PIXELFORMAT_BGRA8);
        }
#endif
#endif
        for (int n : {0,1}) {
            lights_[n].setDirectional(n ? -0.4f : 0.6f,0,-1);
            lights_[n].setDiffuse(1,1,1); lights_[n].setAmbient(0,0,0);
            lights_[n].setShadowArea(Vec3(n ? 68 : 64,48,0),96);
            lights_[n].enableShadow();
        }
        caster_.addVertex(-15,-15,0).addVertex(15,-15,0).addVertex(0,15,0);
        for (int i=0;i<3;++i) caster_.addNormal(0,0,1);
        receiver_ = createPlane(112,80);
        material_.setBaseColor(0.5f,0.5f,0.5f).setRoughness(1);
        fbo_.allocate(128,96);
#if defined(SOKOL_METAL)
        draw_ = events().draw.listen([this] { runMainCases(); });
#else
        draw_ = events().draw.listen([this] { if (b_ && unused_) runCases(); else exitApp(); });
#endif
    }
    void exit() override { clearLights(); }
};
#endif
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    layoutChecks();
    if (argc == 1) return failures ? 1 : 0;
    if (argc != 2 || std::strcmp(argv[1],"--gpu-check")) return 2;
#if (defined(SOKOL_GLCORE) && defined(__linux__)) || (defined(SOKOL_D3D11) && defined(_WIN32)) || defined(SOKOL_METAL)
#if !defined(SOKOL_METAL)
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: secondary windows unavailable"); return failures ? 1 : 0; }
#endif
    WindowSettings settings; settings.setSize(128,96); settings.setHighDpi(false); settings.sampleCount = 1;
    return tcCoreTest::runDisplayTest<ShadowApp>(settings,failures,[] { check("all shadow cases completed",completed); });
#else
    std::puts("SKIP: perWindowShadows GPU suite requires Linux GL, Windows D3D11 or Apple Metal");
    return failures ? 1 : 0;
#endif
}
