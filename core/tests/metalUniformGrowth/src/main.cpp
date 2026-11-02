#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <utility>
#if defined(SOKOL_METAL) && !defined(TC_METAL_GROWTH_SKIP)
#include "../metalProbe.h"
#endif

using namespace tc;
namespace {
#if defined(SOKOL_METAL) && !defined(TC_METAL_GROWTH_SKIP)
static_assert(sizeof(tc_pbr_vs_params_t) == 192, "Update the deterministic split seam for the PBR layout");
int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("metalUniformGrowth %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class GrowthApp : public App {
    Fbo target_;
    Mesh triangle_;
    std::vector<std::vector<unsigned char>> reference_;
    int frame_ = 0;
public:
    void setup() override {
        check("sg_setup starts with the tiny test reserve", sg_query_desc().uniform_buffer_size == 256);
        target_.allocate(96, 64, 1);
        triangle_.addVertex(0, 0, 0).addVertex(18, 0, 0).addVertex(0, 18, 0);
        for (int i = 0; i < 3; ++i) triangle_.addNormal(0, 0, 1);
    }
    void draw() override {
        if (completed) return;
        // Six reference frames, then the same six frames with a tiny reserve.
        // The first tiny frame forces two split PBR uploads in EACH of three
        // passes. Later frames exercise lazy rotate-slot catch-up and reuse.
        if (frame_ == 0 || frame_ == 6) {
            internal::parkWindowPass();
            metalGrowthReserve(frame_ == 0 ? 1024 * 1024 : 256);
        }
        const int sceneFrame = frame_ % 6;
        for (int pass = 0; pass < 3; ++pass) {
            target_.begin(0, 0, 0, 1);
            for (int draw = 0; draw < 6; ++draw) {
                Material material;
                material.setBaseColor(0, 0, 0).setEmissive(
                    Color((draw + 1) / 7.0f, (pass + 1) / 4.0f, (sceneFrame + 1) / 7.0f))
                    .setEmissiveStrength(1);
                setMaterial(material);
                pushMatrix();
                translate(4 + (draw % 3) * 30, 4 + (draw / 3) * 28, 0);
                rotate(0.025f * sceneFrame);
                triangle_.draw();
                popMatrix();
                clearMaterial();
            }
            // Draws are deferred. The wrapper sees the actual consecutive
            // vertex/fragment calls in PbrPipeline::executePbrDraw at end().
            metalGrowthForceSplits(frame_ == 6 ? 2 : 0);
            target_.end();
            std::vector<unsigned char> pixels(96 * 64 * 4);
            check("readback", target_.readPixels(pixels.data()));
            if (frame_ < 6) {
                bool colored = false;
                for (size_t i = 0; i < pixels.size(); i += 4)
                    colored |= pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0;
                check("reference contains PBR geometry", colored);
                reference_.push_back(std::move(pixels));
            } else {
                check("tiny reserve equals large reserve, every RGBA byte",
                      pixels == reference_[sceneFrame * 3 + pass]);
            }
        }
        if (++frame_ == 12) {
            check("six deterministic vertex/fragment splits", metalGrowthSplitCount() == 6);
            check("every rotate slot caught up", metalGrowthSlotsCaughtUp());
            // No open pass: finish the framework target before private backend
            // probes. The attachment is owned by a separate temporary image.
            internal::parkWindowPass();
            sg_tc_mtl_flush();
            sg_image_desc imageDesc = {};
            imageDesc.width = imageDesc.height = 8;
            imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
            imageDesc.usage.color_attachment = true;
            sg_image image = sg_make_image(&imageDesc);
            sg_view_desc viewDesc = {};
            viewDesc.color_attachment.image = image;
            sg_view view = sg_make_view(&viewDesc);
            check("two compute slots survive split growth", metalGrowthComputeCheck());
            // Base's nil-allocation path is unsafe. A detected pixel/compute
            // regression already fails the run; still print the final verdict.
            if (!failures) check("allocation rollback and catch-up", metalGrowthFailureChecks(view));
            sg_destroy_view(view);
            sg_destroy_image(image);
            completed = true;
            exitApp();
        }
    }
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(SOKOL_METAL) && !defined(TC_METAL_GROWTH_SKIP)
    if (argc == 2 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(128, 96).setHighDpi(false).setSwapInterval(0);
        settings.sampleCount = 1;
        RuntimeSettings runtime;
        runtime.reserveUniformBuffer(256);
        return tcCoreTest::runDisplayTest<GrowthApp>(settings, failures, [] {
            check("all twelve frames completed", completed);
            std::printf("metalUniformGrowth: %s (split-PBR=6, frames=12, passes=36, compute, rollback)\n",
                        failures ? "FAIL" : "PASS");
        }, runtime);
    }
#else
    (void)argc; (void)argv;
#endif
    std::printf("SKIP: metalUniformGrowth needs macOS Metal and --gpu-check\n");
    return 0;
}
