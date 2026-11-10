#include <TrussC.h>
#include "tc/gpu/shaders/fboStencil.glsl.h"

namespace trussc::internal {
namespace {
// Host-owned cache: one fixed set for all Fbo sizes and color formats, also
// across hot reload guests. Released before sokol shutdown.
struct StencilResources {
    bool initialized = false;
    bool ready = false;
    sg_shader writeShader = {}, readShader = {};
    sg_shader previewShader = {};
    sg_sampler sampler = {};
    sg_pipeline writes[8] = {}, reads[8] = {};
};
StencilResources& stencilResources() {
    static StencilResources resources;
    return resources;
}

bool ensureStencilResources() {
    auto& s = stencilResources();
    if (s.initialized) return s.ready;
    s.initialized = true;
    const auto r8 = sg_query_pixelformat(SG_PIXELFORMAT_R8);
    if (!r8.render || !r8.blend) {
        logWarning("Fbo") << "stencil round trip requires a blendable R8 render target";
        return false;
    }
    s.writeShader = sg_make_shader(tc_fbostencil_write_shader_desc(sg_query_backend()));
    s.readShader = sg_make_shader(tc_fbostencil_read_shader_desc(sg_query_backend()));
    if (!internalShaderReady(s.writeShader, "Fbo stencil write") ||
        !internalShaderReady(s.readShader, "Fbo stencil read")) return false;
    sg_sampler_desc sampler = {};
    sampler.min_filter = sampler.mag_filter = SG_FILTER_NEAREST;
    s.sampler = sg_make_sampler(&sampler);
    s.ready = sg_query_sampler_state(s.sampler) == SG_RESOURCESTATE_VALID;
    for (int b = 0; b < 8; ++b) {
        sg_pipeline_desc p = {};
        p.shader = s.writeShader;
        // A depth-only pass preserves color and needs no per-format variants.
        p.color_count = 0;
        p.colors[0].pixel_format = SG_PIXELFORMAT_NONE;
        p.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        p.depth.compare = SG_COMPAREFUNC_ALWAYS;
        p.sample_count = 1;
        p.stencil.enabled = true;
        p.stencil.front.compare = SG_COMPAREFUNC_ALWAYS;
        p.stencil.front.pass_op = SG_STENCILOP_REPLACE;
        p.stencil.back = p.stencil.front;
        p.stencil.read_mask = 0xFF;
        p.stencil.write_mask = uint8_t(1 << b);
        p.stencil.ref = uint8_t(1 << b);
        s.writes[b] = sg_make_pipeline(&p);

        p.shader = s.readShader;
        p.color_count = 1;
        p.colors[0].pixel_format = SG_PIXELFORMAT_R8;
        p.colors[0].write_mask = SG_COLORMASK_R;
        p.colors[0].blend.enabled = true;
        p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
        p.stencil.front.compare = SG_COMPAREFUNC_NOT_EQUAL;
        p.stencil.front.pass_op = SG_STENCILOP_KEEP;
        p.stencil.back = p.stencil.front;
        p.stencil.read_mask = uint8_t(1 << b);
        p.stencil.write_mask = 0;
        p.stencil.ref = 0;
        s.reads[b] = sg_make_pipeline(&p);
        s.ready &= sg_query_pipeline_state(s.writes[b]) == SG_RESOURCESTATE_VALID &&
                   sg_query_pipeline_state(s.reads[b]) == SG_RESOURCESTATE_VALID;
    }
    if (!s.ready) logWarning("Fbo") << "stencil round trip resource creation failed";
    return s.ready;
}

void drawStencilBits(bool writing, const Fbo& gray) {
    auto& s = stencilResources();
    for (int b = 0; b < 8; ++b) {
        sg_apply_pipeline(writing ? s.writes[b] : s.reads[b]);
        if (writing) {
            sg_bindings bindings = {};
            bindings.views[VIEW_tc_fbostencil_grayTex] = gray.getTextureView();
            bindings.samplers[SMP_tc_fbostencil_graySmp] = s.sampler;
            sg_apply_bindings(&bindings);
        }
        tc_fbostencil_bit_params_t params = {};
        params.bitValue[0] = float(1 << b);
        sg_apply_uniforms(UB_tc_fbostencil_bit_params, SG_RANGE(params));
        sg_draw(0, 3, 1);
    }
}
} // namespace

bool FboStencilAccess::writeGray(const Fbo& gray, Fbo& destination) {
    if (!gray.allocated_ || !destination.allocated_ || &gray == &destination ||
        gray.target_.activation || destination.target_.activation ||
        gray.target_.sampleCount != 1 || destination.target_.sampleCount != 1 ||
        gray.target_.width != destination.target_.width || gray.target_.height != destination.target_.height ||
        gray.format_ != TextureFormat::R8) {
        logWarning("Fbo") << "stencil write requires distinct, ended, equally sized single-sample owned Fbos and an R8 source";
        return false;
    }
    if (!ensureStencilResources()) return false;
    sg_pass pass = {};
    pass.attachments.depth_stencil = destination.depthAttView_;
    pass.action.depth.load_action = SG_LOADACTION_LOAD;
    // Full-byte replacement: discarded zero bits must not retain old ones.
    pass.action.stencil.load_action = SG_LOADACTION_CLEAR;
    pass.action.stencil.clear_value = 0;
    TargetPassScope scope(pass, destination.target_.width, destination.target_.height,
                          SG_PIXELFORMAT_NONE, 1, true);
    if (!scope.active()) return false;
    drawStencilBits(true, gray);
    return true;
}

bool FboStencilAccess::readR8(const Fbo& source, Fbo& destination) {
    if (!source.allocated_ || !destination.allocated_ || &source == &destination ||
        source.target_.activation || destination.target_.activation ||
        source.target_.sampleCount != 1 || destination.target_.sampleCount != 1 ||
        source.target_.width != destination.target_.width || source.target_.height != destination.target_.height ||
        destination.format_ != TextureFormat::R8 || destination.mipmaps_) {
        logWarning("Fbo") << "stencil read requires distinct, ended, equally sized single-sample owned Fbos and an R8 target without mipmaps";
        return false;
    }
    if (!ensureStencilResources()) return false;
    destination.syncVersionFrame_();
    if (destination.drawnThisFrame_ && !destination.advanceVersion_()) return false;
    destination.drawnThisFrame_ = false;
    sg_pass pass = {};
    pass.attachments.colors[0] = destination.curColorTex_().getAttachmentView();
    pass.attachments.depth_stencil = source.depthAttView_;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0, 0, 0, 0};
    pass.action.depth.load_action = SG_LOADACTION_LOAD;
    pass.action.stencil.load_action = SG_LOADACTION_LOAD;
    TargetPassScope scope(pass, source.target_.width, source.target_.height, SG_PIXELFORMAT_R8, 1, true);
    if (!scope.active()) return false;
    drawStencilBits(false, source);
    return true;
}

void FboStencilAccess::shutdown() {
    auto& s = stencilResources();
    for (auto p : s.writes) if (p.id) sg_destroy_pipeline(p);
    for (auto p : s.reads) if (p.id) sg_destroy_pipeline(p);
    if (s.sampler.id) sg_destroy_sampler(s.sampler);
    if (s.writeShader.id) sg_destroy_shader(s.writeShader);
    if (s.readShader.id) sg_destroy_shader(s.readShader);
    if (s.previewShader.id) sg_destroy_shader(s.previewShader);
    s = {};
}
} // namespace trussc::internal

namespace trussc {
Fbo* Fbo::captureStencil_(bool perFrame) const {
    if (!allocated_ || target_.activation || target_.sampleCount != 1) {
        if (!perFrame || !warnedStencilPreview_) {
            if (perFrame) warnedStencilPreview_ = true;
            logWarning("Fbo") << "stencil diagnostics require an ended, allocated, single-sample owned Fbo; main targets and MSAA are unsupported";
        }
        return nullptr;
    }
    if (!stencilCache_) {
        stencilCache_ = std::make_unique<Fbo>();
        stencilCache_->allocate(target_.width, target_.height, 1, TextureFormat::R8);
        stencilCache_->getTexture().setFilter(TextureFilter::Nearest);
    }
    return internal::FboStencilAccess::readR8(*this, *stencilCache_) ? stencilCache_.get() : nullptr;
}

bool Fbo::readStencil(Pixels& pixels) const {
    if (internal::hasPublicFboActivation()) {
        logWarning("Fbo") << "readStencil() requires end() on all active Fbos";
        return false;
    }
    auto* cache = captureStencil_(false);
    if (!cache) return false;
    Pixels rgba;
    rgba.allocate(target_.width, target_.height, 4);
    if (!cache->readPixels(rgba.getData())) return false;
    pixels.allocate(target_.width, target_.height, 1, PixelFormat::U8);
    const size_t count = size_t(target_.width) * size_t(target_.height);
    for (size_t i = 0; i < count; ++i) pixels.getData()[i] = rgba.getData()[i * 4];
    return true;
}

void Fbo::drawStencil(float x, float y) const {
    auto* cache = captureStencil_(true);
    if (!cache) return;
    auto& s = internal::stencilResources();
    if (!s.previewShader.id) s.previewShader = sg_make_shader(tc_fbostencil_preview_shader_desc(sg_query_backend()));
    if (!internal::internalShaderReady(s.previewShader, "Fbo stencil preview")) return;
    auto& target = internal::currentTarget();
    internal::PipelineKey key;
    key.role = internal::PipelineRole::Shader;
    key.desc = internal::pipeDesc2D(BlendMode::Disabled, target.depthTestEnabled);
    key.desc.shader = s.previewShader;
    key.desc.layout = internal::sglColorVertexLayout();
    const auto pipeline = target.pipeline(key).immediate;
    if (!pipeline.id) return;
    cache->markDrawnThisFrame_();
    internal::loadTemporaryPipeline(pipeline);
    sgl_enable_texture();
    sgl_texture(cache->getTextureView(), cache->getSampler());
    const float topV = needsGlYFlip() ? 1.0f : 0.0f;
    sgl_begin_quads();
    sgl_c4f(1, 1, 1, 1);
    sgl_v2f_t2f(x, y, 0, topV);
    sgl_v2f_t2f(x + target_.width, y, 1, topV);
    sgl_v2f_t2f(x + target_.width, y + target_.height, 1, 1 - topV);
    sgl_v2f_t2f(x, y + target_.height, 0, 1 - topV);
    sgl_end();
    sgl_disable_texture();
    internal::restoreCurrentPipeline();
}
} // namespace trussc
