#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

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
    internal::WindowRequestAccess::HostState unusedState; internal::WindowContext& unused = unusedState.context;
    check("unused window has no shadow generation", !unused.shadow.storage);
}
#if (defined(SOKOL_GLCORE) && defined(__linux__)) || (defined(SOKOL_GLES3) && defined(__linux__) && !defined(__ANDROID__)) || (defined(SOKOL_D3D11) && defined(_WIN32)) || defined(SOKOL_METAL)
class ShadowApp : public App {
    internal::WindowContext* a_ = nullptr;
    std::shared_ptr<Window> b_, unused_;
    internal::WindowRequestAccess::HostState unusedState_;
    Light lights_[2];
    Mesh caster_, receiver_;
    Material material_;
    Fbo fbo_;
    EventListener mainDraw_, mainAfter_, log_;
    int unexpectedErrors_ = 0;
    int capturePair_ = 0;
    int mainFrames_ = 0;
    int closeWait_ = 0;
    bool secondaryPresented_ = false;
    bool unusedDrew_ = false;
    bool crossWindowCases_ = false;
    bool closingOwner_ = false;
    bool secondarySkipped_ = false;
    sg_image closingImage_{};
    std::weak_ptr<internal::ShadowStorage> closing_;

    // Each step belongs to one native tick. Only afterFrame advances the plan,
    // so a comparison never races ahead of either window's presentation.
    struct Step {
        int window;
        std::function<void()> draw, after;
    };
    std::vector<Step> steps_;
    size_t step_ = 0;
    bool drewStep_[2] = {};
    internal::WindowContext& owner(int n) {
        return n ? trussc::internal::WindowRequestAccess::context(*b_) : *a_;
    }
    void add(int n, std::function<void()> draw, std::function<void()> after = {}) {
        steps_.push_back({n, std::move(draw), std::move(after)});
    }
    void requestStep() {
        if (step_ < steps_.size()) owner(steps_[step_].window).loop.redrawCount = 1;
    }
    void start(int n, int resolution) {
        check("case runs in its owner's native tick", &internal::currentWindowContext() == &owner(n) && owner(n).tickActive);
        beginFrame();
        // afterFrame can stage the next logical recording before a foreign
        // commit. Give that recording a fresh shadow-slot key too.
        ++owner(n).resourceTickCount;
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
    void shadow(int n, bool two = false, bool occluded = true) {
        beginShadowPass(lights_[n]);
        if (occluded) { cast(n); if (two) cast(n,true); }
        endShadowPass();
        internal::parkWindowPass();
    }
    void queue(bool fbo = false) {
        if (fbo) fbo_.begin(0,0,0,1);
        setMaterial(material_);
        pushMatrix(); translate(64,48,0); receiver_.draw(); popMatrix();
        clearMaterial();
    }
    void scene(int n, int res, bool fbo = false, bool two = false, bool occluded = true) {
        start(n,res); shadow(n,two,occluded); queue(fbo);
        if (fbo) fbo_.end();
    }
    Pixels capture(bool fbo = false) {
        check("capture runs in its owner's native tick",
            &internal::currentWindowContext() == &owner(steps_[step_].window) && internal::currentWindowContext().tickActive);
        Pixels pixels;
        if (fbo) {
            pixels.allocate(128,96,4);
            check("Fbo readback", fbo_.readPixels(pixels.getData()));
        } else check("screen readback", captureWindow(pixels));
        return pixels;
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
            const auto prefix = fs::path(directory) / std::to_string(capturePair_);
            check("actual image saved", actual.save(prefix.string()+"-actual.png").ok());
            check("reference image saved", reference.save(prefix.string()+"-reference.png").ok());
        }
        ++capturePair_;
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
        struct Pair {
            Pixels reference[2], actualSecond;
            internal::RenderTarget* target = nullptr;
            std::shared_ptr<internal::ShadowStorage> generation;
        };
        auto p = std::make_shared<Pair>();
        const int second = 1-first;
        const int sizes[] = {resA,resB};
        add(first, [=,this] { scene(first,sizes[first],fbo); },
            [=,this] { p->reference[first] = capture(fbo); });
        add(second, [=,this] { scene(second,sizes[second]); },
            [=,this] { p->reference[second] = capture(); });
        // Record after presentation, when the tick still belongs to this
        // window. No screen pass is opened until its next requested tick.
        add(first, {}, [=,this] {
            start(first,sizes[first]); shadow(first); queue(fbo);
            p->target = &internal::currentTarget();
            const auto& pending = p->target->deferredPbrDraws;
            p->generation = owner(first).shadow.storage;
            check("first PBR is deferred", pending.size() == 1);
            check("PBR pins target owner's generation", pending.size() == 1 &&
                pending.back().cmd.shadowGeneration == p->generation &&
                pending.back().cmd.bind.views[VIEW_tc_pbr_shadowMap].id == p->generation->colorTexture.id);
        });
        add(second, [=,this] { scene(second,sizes[second]); }, [=,this] {
            p->actualSecond = capture();
            const auto& gen = p->generation;
            const auto other = owner(second).shadow.storage;
            check("owners have distinct images/views/samplers", gen != other &&
                gen->colorImage.id != other->colorImage.id && gen->colorTexture.id != other->colorTexture.id &&
                gen->depthImage.id != other->depthImage.id && gen->sampler.id != other->sampler.id);
            check("foreign commit preserves lazy PBR or its consumed Fbo output",
                p->target->deferredPbrDraws.size() == (fbo ? 0u : 1u));
        });
        add(first, [=,this] { if (fbo) fbo_.end(); }, [=,this] {
            matches("first owner matches isolated reference",capture(fbo),p->reference[first]);
            matches("second owner matches isolated reference",p->actualSecond,p->reference[second]);
            p->generation.reset();
        });
    }
    void fboAfterForeign(int first) {
        auto reference = std::make_shared<Pixels>();
        const int sizes[] = {64,128};
        const int second = 1-first;
        add(first, [=,this] { scene(first,sizes[first],true); },
            [=,this] { *reference = capture(true); });
        add(first, {}, [=,this] { start(first,sizes[first]); shadow(first); });
        add(second, [=,this] { scene(second,sizes[second]); });
        add(first, [this] { queue(true); fbo_.end(); }, [=,this] {
            matches("Fbo PBR after foreign shadow writes uses its owner maps",capture(true),*reference);
        });
    }
    void mainCases() {
        add(0, [this] {
            check("unused window context allocates zero shadow bytes", !unusedState_.context.shadow.storage);
            start(0,1024); queue();
            const auto& pending = a_->swapchainTarget.deferredPbrDraws;
            check("PBR without shadows has no shadow generation", !a_->shadow.storage &&
                pending.size() == 1 && !pending.back().cmd.shadowGeneration);
        });
        add(0, [this] {
            start(0,1024); queue(true);
            const auto& pending = internal::currentTarget().deferredPbrDraws;
            check("main Fbo PBR without shadows has no shadow generation", !a_->shadow.storage &&
                pending.size() == 1 && !pending.back().cmd.shadowGeneration);
            fbo_.end();
        });
        add(0, [this] { scene(0,1024); }, [this] {
            capture();
            check("main shadow default remains 1024", a_->shadow.storage && a_->shadow.storage->resolution == 1024);
        });
        auto defaultFbo = std::make_shared<Pixels>();
        add(0, [this] { scene(0,1024,true); }, [=,this] { *defaultFbo = capture(true); });
        add(0, [this] { scene(0,1024,true); }, [=,this] {
            matches("single-owner Fbo PBR matches isolated reference",capture(true),*defaultFbo);
            a_->shadow.storage.reset(); a_->shadow.count = 0;
        });
        auto visible = std::make_shared<Pixels>();
        add(0, [this] { scene(0,64); }, [=,this] { *visible = capture(); });
        add(0, [this] { scene(0,64,false,false,false); }, [=,this] {
            const auto count = different(*visible,capture());
            check("reference contains a visible cast shadow", count > 40 && count != SIZE_MAX);
        });
        auto fboVisible = std::make_shared<Pixels>();
        add(0, [this] { scene(0,64,true); }, [=,this] { *fboVisible = capture(true); });
        add(0, [this] { scene(0,64,true,false,false); }, [=,this] {
            const auto count = different(*fboVisible,capture(true));
            check("Fbo reference contains a visible cast shadow", count > 40 && count != SIZE_MAX);
        });
        auto reference = std::make_shared<Pixels>();
        add(0, [this] { scene(0,256); }, [=,this] { *reference = capture(); });
        for (bool child : {false,true}) {
            add(0, [=,this] {
                start(0,256); shadow(0); queue();
                check("same-owner prior PBR starts lazy", !a_->swapchainTarget.inPass);
                ++a_->resourceTickCount;
                if (child) fbo_.begin(0,0,0,1);
                beginShadowPass(lights_[0]); cast(1); endShadowPass();
                if (child) fbo_.end();
                check("target-stack entry consumed prior PBR", a_->swapchainTarget.deferredPbrDraws.empty());
            }, [=,this] {
                matches("same-owner rewrite preserves earlier screen pixels",capture(),*reference);
            });
        }
        add(0, {}, [this] { a_->shadow.storage.reset(); a_->shadow.count = 0; });
    }
    void growthCases(bool foreign, bool fbo = false) {
        struct Growth {
            Pixels reference;
            std::weak_ptr<internal::ShadowStorage> old;
            sg_image image{};
            sg_view view{};
        };
        auto growth = std::make_shared<Growth>();
        add(0, [=,this] {
            // Storage never shrinks; each variant must actually grow from 64 to 256.
            a_->shadow.storage.reset(); a_->shadow.count = 0;
            scene(0,64,fbo);
        }, [=,this] { growth->reference = capture(fbo); });
        add(0, {}, [=,this] {
            start(0,64); shadow(0); queue(fbo);
            auto& pending = internal::currentTarget().deferredPbrDraws;
            growth->old = a_->shadow.storage;
            growth->image = a_->shadow.storage->colorImage;
            growth->view = a_->shadow.storage->colorTexture;
            check(fbo ? "main Fbo PBR is deferred and pins its owner generation/view" :
                "main screen PBR is deferred and pins its owner generation/view", pending.size() == 1 &&
                pending.back().cmd.shadowGeneration == growth->old.lock() &&
                pending.back().cmd.bind.views[VIEW_tc_pbr_shadowMap].id == growth->view.id);
            check("growth publishes a complete new generation", internal::ensureShadowStorage(*a_,256) &&
                a_->shadow.storage != growth->old.lock() && a_->shadow.storage->resolution == 256 &&
                a_->shadow.count == 0 &&
                pending.size() == 1 && pending.back().cmd.shadowGeneration == growth->old.lock() &&
                pending.back().cmd.bind.views[VIEW_tc_pbr_shadowMap].id == growth->view.id);
            check("growth preserves older recording resources", !growth->old.expired() &&
                sg_query_image_state(growth->image) == SG_RESOURCESTATE_VALID &&
                sg_query_view_state(growth->view) == SG_RESOURCESTATE_VALID);
        });
        if (foreign) add(1, [this] { scene(1,128); }, [=,this] {
            check("old generation survives foreign retirement", !growth->old.expired() &&
                sg_query_image_state(growth->image) == SG_RESOURCESTATE_VALID &&
                sg_query_view_state(growth->view) == SG_RESOURCESTATE_VALID);
        });
        add(0, [=,this] { if (fbo) fbo_.end(); }, [=,this] {
            matches(fbo ? "older Fbo recording renders after growth" : "older recording renders after growth",
                capture(fbo),growth->reference);
            check("old generation retires after consumer commit", growth->old.expired() &&
                sg_query_image_state(growth->image) == SG_RESOURCESTATE_INVALID &&
                sg_query_view_state(growth->view) == SG_RESOURCESTATE_INVALID);
        });
        if (fbo) {
            auto reference = std::make_shared<Pixels>();
            add(0, [this] { scene(0,256,true); }, [=,this] { *reference = capture(true); });
            add(0, [this] { scene(0,256,true); }, [=,this] {
                matches("grown Fbo PBR matches isolated reference",capture(true),*reference);
            });
        }
    }
    void crossWindowCases() {
        pair(0,1024,1024);
        add(0, {}, [this] {
            check("default unchanged for both windows", a_->shadow.storage->resolution == 1024 && owner(1).shadow.storage->resolution == 1024);
            reportBytes();
            check("real unused window allocates zero shadow bytes", !trussc::internal::WindowRequestAccess::context(*unused_).shadow.storage);
            a_->shadow.storage.reset(); owner(1).shadow.storage.reset();
            a_->shadow.count = owner(1).shadow.count = 0;
        });
        pair(1,64,128);
        add(0, {}, [this] { reportBytes(); });
        pair(0,64,128,true);
        pair(1,64,128,true);
        fboAfterForeign(0);
        fboAfterForeign(1);
        auto withShadow = std::make_shared<Pixels>();
        add(0, [this] { scene(0,64); }, [=,this] { *withShadow = capture(); });
        add(1, [this] { scene(1,128); }, [=,this] {
            const auto count = different(*withShadow,capture());
            check("A and B have deliberately different shadow pixels", count > 40 && count != SIZE_MAX);
        });

        growthCases(true);
        growthCases(false,true);
        pair(0,256,128);
        add(0, {}, [this] { reportBytes(); });

        struct Nested { Pixels reference; sg_view attachment{}; uint64_t token = 0; };
        auto nested = std::make_shared<Nested>();
        add(0, [this] { scene(0,256,false,true); }, [=,this] { nested->reference = capture(); });
        add(0, {}, [=,this] {
            start(0,256); beginShadowPass(lights_[0]); cast(0);
            nested->attachment = a_->shadow.target.binding.attachments.colors[0];
            nested->token = a_->shadow.target.activation;
        });
        add(1, [this] { scene(1,128); });
        add(0, [=,this] {
            check("foreign tick parks an owner-local open shadow pass", a_->shadow.inPass &&
                !a_->shadow.target.inPass && a_->shadow.target.activation == nested->token && a_->shadow.current == 0);
            cast(0,true);
            check("shadow pass resumes same attachment", a_->shadow.target.inPass &&
                a_->shadow.target.binding.attachments.colors[0].id == nested->attachment.id);
            endShadowPass(); internal::parkWindowPass(); queue();
        }, [=,this] {
            matches("nested open shadow pass matches isolated reference",capture(),nested->reference);
        });
        add(1, {}, [this] {
            start(1,128); shadow(1); queue();
            closingImage_ = owner(1).shadow.storage->colorImage;
            closing_ = owner(1).shadow.storage;
        });
        add(0, [this] {
            b_->close();
            closingOwner_ = true;
            setFps(VSYNC);
        });
    }
    void finishCases() {
        check("all expected comparison pairs ran", capturePair_ == (crossWindowCases_ ? 19 : 7));
        check("unused window PBR case ran", !crossWindowCases_ || unusedDrew_);
        check("unused window still allocates zero shadow bytes", !unused_ ||
            !trussc::internal::WindowRequestAccess::context(*unused_).shadow.storage);
        check("unused window context still allocates zero shadow bytes", !unusedState_.context.shadow.storage);
        check("no unexpected rendering errors", unexpectedErrors_ == 0);
        std::printf("perWindowShadows: %d comparison pairs completed\n",capturePair_);
        completed = true;
        exitApp();
    }
    void checkNativeTarget() {
        auto& ctx = internal::currentWindowContext();
        const auto sc = ctx.acquireSwapchain(ctx.acquireSwapchainUser);
        check("native target available in its own tick", ctx.tickActive && !sc.invalid &&
            sc.width == 128 && sc.height == 96 && sc.sample_count == 1 &&
            sc.color_format != SG_PIXELFORMAT_NONE && sc.depth_format != SG_PIXELFORMAT_NONE);
#if defined(SOKOL_D3D11) && defined(_WIN32)
        check("D3D11 native target views and BGRA8 format", sc.d3d11.render_view &&
            sc.d3d11.depth_stencil_view && !sc.d3d11.resolve_view &&
            sc.color_format == SG_PIXELFORMAT_BGRA8);
#endif
    }
    void drawWindow(int n) {
        if (closingOwner_) {
            if (n) return;
            // Bound (native close): allow 60 main frames for the requested teardown to reach its safe point.
            if (b_->isOpen() && ++closeWait_ < 60) return;
            check("requested owner closed at native safe point", !b_->isOpen());
            internal::endGpuFrame();
            check("close retires pending owner maps", closing_.expired() &&
                !owner(1).shadow.storage && sg_query_image_state(closingImage_) == SG_RESOURCESTATE_INVALID);
            check("closing B preserves A maps", a_->shadow.storage &&
                sg_query_image_state(a_->shadow.storage->colorImage) == SG_RESOURCESTATE_VALID);
            finishCases();
            return;
        }
        if (step_ < steps_.size() && steps_[step_].window == n) {
            drewStep_[n] = true;
            if (steps_[step_].draw) steps_[step_].draw();
        }
    }
    void afterWindow(int n) {
        if (n == 0) ++mainFrames_;
        if (n == 1 && !secondaryPresented_) {
            checkNativeTarget();
            secondaryPresented_ = true;
            b_->setFps(EVENT_DRIVEN);
        }
        if (drewStep_[n]) {
            drewStep_[n] = false;
            if (steps_[step_].after) steps_[step_].after();
            // End encoders without consuming a lazy swapchain recording or
            // dropping a target activation. The next owner can now commit.
            internal::parkWindowPass();
            ++step_;
            requestStep();
        }
        if (n || step_ != steps_.size() || closingOwner_) return;
        if (crossWindowCases_) return;
        if (secondarySkipped_) { finishCases(); return; }
        // Bound (presentation admission): wait at most 600 main frames for the secondary's first presentation.
        if (secondaryPresented_) {
            crossWindowCases_ = true;
            steps_.clear(); step_ = 0;
            crossWindowCases();
            setFps(EVENT_DRIVEN);
            requestStep();
        } else if (mainFrames_ >= 600) {
            std::puts("SKIP: perWindowShadows secondary-window cases: the secondary window never presented within 600 main frames (occluded or unavailable on this host); continuing main-window cases");
            secondarySkipped_ = true;
            steps_.clear(); step_ = 0;
            growthCases(false);
            growthCases(false,true);
            auto reference = std::make_shared<Pixels>();
            add(0, [this] { scene(0,256); }, [=,this] { *reference = capture(); });
            add(0, [this] { scene(0,256); }, [=,this] {
                matches("grown PBR matches isolated reference",capture(),*reference);
            });
            requestStep();
        }
    }
    void drawUnused() {
        unusedDrew_ = true;
        check("unused PBR runs in its own tick", internal::currentWindowContext().tickActive &&
            &internal::currentWindowContext() == &trussc::internal::WindowRequestAccess::context(*unused_));
        clear(0,0,0,1); queue();
        const auto& ctx = internal::currentWindowContext();
        check("unused PBR without shadows has no shadow generation", !ctx.shadow.storage &&
            ctx.swapchainTarget.deferredPbrDraws.size() == 1 &&
            !ctx.swapchainTarget.deferredPbrDraws.back().cmd.shadowGeneration);
    }
    // Event subscriptions inherit their App's window. Install each driver's
    // afterFrame listener from that window's own App::setup(), too.
    class WindowDriver : public App {
        ShadowApp* coordinator_;
        int window_;
        EventListener draw_, after_;
    public:
        WindowDriver(ShadowApp* coordinator, int window) : coordinator_(coordinator), window_(window) {}
        void setup() override {
            getWindow()->clearColorSet_ = false;
            draw_ = events().draw.listen([this] {
                if (window_ == 1) coordinator_->drawWindow(1);
                else coordinator_->drawUnused();
            }, EventPriority::BeforeApp);
            after_ = events().afterFrame.listen([this] {
                if (window_ == 1) coordinator_->afterWindow(1);
                else coordinator_->checkNativeTarget();
            });
        }
        void draw() override {}
        void exit() override { clearLights(); }
    };
public:
    void setup() override {
        a_ = &internal::currentWindowContext();
        // These internal recording tests deliberately carry work into the next
        // tick. Clear only in start(), so the host cannot erase that work.
        getWindow()->clearColorSet_ = false;
        log_ = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level >= LogLevel::Error) ++unexpectedErrors_;
        });
        if (internal::supportsSecondaryWindows()) {
            WindowSettings settings; settings.setSize(128,96); settings.setHighDpi(false); settings.sampleCount = 1;
            b_ = createWindow(settings); unused_ = createWindow(settings);
            check("secondary and unused windows created", b_ && unused_);
            if (!b_ || !unused_) { exitApp(); return; }
            b_->setPosition(160,32);
            b_->setApp<WindowDriver>(this,1);
            unused_->setPosition(320,32);
            unused_->setFps(EVENT_DRIVEN);
            trussc::internal::WindowRequestAccess::context(*unused_).loop.redrawCount = 1;
            unused_->setApp<WindowDriver>(this,2);
        }
        check("all windows start without shadow maps", !a_->shadow.storage &&
            (!b_ || !owner(1).shadow.storage) &&
            (!unused_ || !trussc::internal::WindowRequestAccess::context(*unused_).shadow.storage));
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
        mainCases();
        // Resume before Node::drawTree's matrix scope and open-target check:
        // a continuation ends the Fbo/shadow parked by an earlier native tick.
        mainDraw_ = events().draw.listen([this] { drawWindow(0); }, EventPriority::BeforeApp);
        mainAfter_ = events().afterFrame.listen([this] { afterWindow(0); });
    }
    void draw() override {}
    void exit() override { clearLights(); }
};
#endif
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    layoutChecks();
    if (argc == 1) return failures ? 1 : 0;
    if (argc != 2 || std::strcmp(argv[1],"--gpu-check")) return 2;
#if (defined(SOKOL_GLCORE) && defined(__linux__)) || (defined(SOKOL_GLES3) && defined(__linux__) && !defined(__ANDROID__)) || (defined(SOKOL_D3D11) && defined(_WIN32)) || defined(SOKOL_METAL)
    WindowSettings settings; settings.setSize(128,96); settings.setHighDpi(false); settings.sampleCount = 1;
    return tcCoreTest::runDisplayTest<ShadowApp>(settings,failures,[] { check("all shadow cases completed",completed); });
#else
    std::puts("SKIP: perWindowShadows GPU suite requires Linux GL/GLES, Windows D3D11 or Apple Metal");
    return failures ? 1 : 0;
#endif
}
