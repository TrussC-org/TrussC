// #362: slot validation and binding copies without a window or GPU setup.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

using namespace tc;

namespace {
int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

template<class Base>
struct InspectShader : Base {
    using Base::fillTextureBindings;
    using Base::imageViews_;
    using Base::pendingUniforms;
    using Base::pendingViews;
};

// Also detect writes outside sg_bindings, not just into its end canary.
struct GuardedBindings {
    std::array<uint32_t, 64> before;
    sg_bindings bindings;
    std::array<uint32_t, 64> after;
};

template<class Base>
void checkBindingCopies() {
    InspectShader<Base> shader;
    // Each valid slot separately checks both sides of the sampler boundary
    // and the last view slot; zero and nonzero sentinels must be preserved.
    for (int slot = 0; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
        shader.pendingViews.clear();
        shader.setTexture(slot, sg_view{101}, sg_sampler{202});
        check("valid texture slot is stored", shader.pendingViews.size() == 1 &&
            shader.pendingViews.count(slot) == 1);
        for (int sentinel : {0, 0x5a}) {
            GuardedBindings actual;
            std::memset(&actual, sentinel, sizeof(actual));
            GuardedBindings expected;
            std::memcpy(&expected, &actual, sizeof(actual));
            expected.bindings.views[slot].id = 101;
            if (slot < SG_MAX_SAMPLER_BINDSLOTS) {
                expected.bindings.samplers[slot].id = 202;
            }
            shader.fillTextureBindings(actual.bindings);
            check("only the intended view/sampler bytes change (including canaries)",
                std::memcmp(&actual, &expected, sizeof(actual)) == 0);
        }
    }
    // A complete set also proves high views cannot overwrite low samplers.
    GuardedBindings actual{};
    GuardedBindings expected{};
    std::memset(&actual, 0, sizeof(actual));
    std::memset(&expected, 0, sizeof(expected));
    for (int slot = 0; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
        auto id = static_cast<uint32_t>(slot + 1);
        shader.setTexture(slot, sg_view{id}, sg_sampler{id + 100});
        expected.bindings.views[slot].id = id;
        if (slot < SG_MAX_SAMPLER_BINDSLOTS) {
            expected.bindings.samplers[slot].id = id + 100;
        }
    }
    shader.fillTextureBindings(actual.bindings);
    check("all view slots coexist with the supported sampler slots",
        std::memcmp(&actual, &expected, sizeof(actual)) == 0);
}
} // namespace

TC_CORE_TEST_MAIN() {
    check("sokol is not initialized", !sg_isvalid());
    int textureWarnings = 0, samplerWarnings = 0, uniformWarnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning) return;
        if (e.message.find("Texture slot") != std::string::npos) ++textureWarnings;
        if (e.message.find("Sampler for texture slot") != std::string::npos &&
            e.message.find("ignored") != std::string::npos) ++samplerWarnings;
        if (e.message.find("Uniform slot") != std::string::npos) ++uniformWarnings;
    });
    InspectShader<Shader> shader;
    const int invalidTextures[] = {-1, SG_MAX_VIEW_BINDSLOTS,
        SG_MAX_VIEW_BINDSLOTS + 8, INT_MIN, INT_MAX};
    for (int slot : invalidTextures) {
        shader.setTexture(slot, sg_view{101}, sg_sampler{202});
        // A valid-looking image would create a view without the early guard.
        shader.setTexture(slot, sg_image{303}, sg_sampler{202});
        check("invalid texture slot leaves both maps empty",
            shader.pendingViews.empty() && shader.imageViews_.empty());
    }
    for (int slot = SG_MAX_SAMPLER_BINDSLOTS; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
        shader.setTexture(slot, sg_view{101}, sg_sampler{SG_INVALID_ID});
    }
    check("high texture slots without a sampler do not warn", samplerWarnings == 0);
    checkBindingCopies<Shader>();
    checkBindingCopies<FullscreenShader>();

    const int invalidUniforms[] = {-1, SG_MAX_UNIFORMBLOCK_BINDSLOTS,
        SG_MAX_UNIFORMBLOCK_BINDSLOTS + 8, INT_MIN, INT_MAX};
    for (int slot : invalidUniforms) {
        shader.setUniform(slot, 1.0f);
        // Rejection must precede accessing/copying the data as well.
        shader.setUniform(slot, nullptr, sizeof(float));
        check("invalid uniform slot leaves map empty", shader.pendingUniforms.empty());
    }
    for (int slot = 0; slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++slot) {
        float value = static_cast<float>(slot + 1);
        shader.setUniform(slot, value);
        const float expected[] = {value, 0, 0, 0};
        const auto& bytes = shader.pendingUniforms.at(slot);
        check("valid uniform slot retains its padded bytes", bytes.size() == sizeof(expected) &&
            std::memcmp(bytes.data(), expected, sizeof(expected)) == 0);
    }
    auto viewsBefore = shader.pendingViews;
    auto uniformsBefore = shader.pendingUniforms;
    for (int repeat = 0; repeat < 100; ++repeat) {
        for (int slot : invalidTextures) {
            shader.setTexture(slot, sg_view{999}, sg_sampler{999});
            shader.setTexture(slot, sg_image{999}, sg_sampler{999});
        }
        for (int slot : invalidUniforms) shader.setUniform(slot, 999.0f);
    }
    bool viewsUnchanged = shader.pendingViews.size() == viewsBefore.size();
    for (const auto& [slot, binding] : viewsBefore) {
        const auto& current = shader.pendingViews.at(slot);
        viewsUnchanged = viewsUnchanged && current.view.id == binding.view.id &&
            current.sampler.id == binding.sampler.id;
    }
    check("rejected calls preserve existing bindings and image cache",
        viewsUnchanged && shader.pendingUniforms == uniformsBefore && shader.imageViews_.empty());
    check("texture range warning occurs once at its shared call site", textureWarnings == 1);
    check("ignored sampler warning occurs once at its shared call site", samplerWarnings == 1);
    check("uniform range warning occurs once at its call site", uniformWarnings == 1);
    check("test never initializes sokol", !sg_isvalid());
    std::printf("shaderBindingSlots: %d failures\n", failures);
    return failures ? 1 : 0;
}
