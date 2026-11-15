// #1386: packed stencil values participate in selection cache identity.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace tc;
using namespace tc::internal;
int failures = 0;
void check(const char* label, bool passed) {
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) ++failures;
}
}

TC_CORE_TEST_MAIN() {
    const auto standard = fillSelection(BlendMode::Alpha);
    const auto defaultId = PipelineSelectionId::immediate(standard);
    check("default stencil key is zero", standard.stencilDescKey == 0 && defaultId.stencilDescKey == 0);
    PipelineSelectionCache cache;
    cache.emplace(defaultId, PipelineHandles{{1}});
    bool distinct = true, copied = true;
    for (unsigned bit = 0; bit < 21; ++bit) {
        auto selection = standard;
        selection.stencilDescKey = uint32_t(1) << bit;
        auto id = PipelineSelectionId::immediate(selection);
        copied = copied && id.stencilDescKey == selection.stencilDescKey;
        distinct = distinct && !(selection == standard) && !(id == defaultId);
        cache.emplace(id, PipelineHandles{{uint32_t(bit + 2)}});
    }
    check("all five stencil fields distinguish immediate selections", distinct);
    check("packed stencil key reaches shared selection identity", copied);
    bool retained = cache.size() == 22 && cache.at(defaultId).immediate.id == 1;
    for (unsigned bit = 0; bit < 21; ++bit) {
        auto selection = standard;
        selection.stencilDescKey = uint32_t(1) << bit;
        retained = retained && cache.at(PipelineSelectionId::immediate(selection)).immediate.id == bit + 2;
    }
    check("alternating keys retain independent cached handles", retained);
    size_t previousHash = 2166136261u;
    for (auto value : {defaultId.recipe, defaultId.variant, defaultId.attachments}) {
        previousHash = (previousHash ^ uint32_t(value)) * 16777619u;
        previousHash = (previousHash ^ uint32_t(value >> 32)) * 16777619u;
    }
    check("default hash preserves previous calculation", PipelineSelectionHash{}(defaultId) == previousHash);
    check("stencil enum labels match values", std::strcmp(enumLabel(StencilTest::Always), "Always") == 0 &&
        std::strcmp(enumLabel(StencilTest::GreaterEqual), "GreaterEqual") == 0);
    bool raster = true;
    for (int compare = 0; compare < 8; ++compare) {
        for (int mask : {0, 1, 0x0F, 0x80, 0xFF}) {
            for (bool write : {false, true}) for (bool color : {false, true}) {
                StencilDesc desc{173, StencilTest(compare), write, uint8_t(mask), color};
                sg_pipeline_desc pipeline = {};
                applyStencilDesc(pipeline, stencilDescKey(desc));
                raster &= pipeline.stencil.enabled && pipeline.stencil.ref == 173 &&
                    pipeline.stencil.front.compare == sg_compare_func(compare ? compare : SG_COMPAREFUNC_ALWAYS) &&
                    pipeline.stencil.back.compare == sg_compare_func(compare ? compare : SG_COMPAREFUNC_ALWAYS) &&
                    pipeline.stencil.front.fail_op == SG_STENCILOP_KEEP &&
                    pipeline.stencil.front.depth_fail_op == SG_STENCILOP_KEEP &&
                    pipeline.stencil.front.pass_op == (write ? SG_STENCILOP_REPLACE : SG_STENCILOP_KEEP) &&
                    pipeline.stencil.read_mask == mask && pipeline.stencil.write_mask == (write ? mask : 0) &&
                    (pipeline.colors[0].write_mask == SG_COLORMASK_NONE) == !color;
            }
        }
    }
    check("packed fields apply all comparisons, masks, write and color combinations", raster);
    bool roundTrip = true;
    for (unsigned value = 0; value < 256; ++value) {
        for (unsigned mask = 0; mask < 256; ++mask) {
            for (unsigned flags = 0; flags < 32; ++flags) {
                const StencilDesc desc{uint8_t(value), StencilTest(flags & 7),
                    bool(flags & 8), uint8_t(mask), bool(flags & 16)};
                roundTrip &= stencilDescFromKey(stencilDescKey(desc)) == desc;
            }
        }
    }
    check("packed descriptor round trips every field combination", roundTrip);
    return failures ? 1 : 0;
}
