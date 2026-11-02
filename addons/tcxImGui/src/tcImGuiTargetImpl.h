// Included once by each platform's implementation unit. Keep sokol_imgui intact:
// capture its two immutable recipes; pipelines belong exclusively to the target.
// imgui_draw.cpp leaves these implementation switches defined. TrussC also
// includes stb declarations; do not compile a second stb implementation here.
#undef STB_TRUETYPE_IMPLEMENTATION
#undef STB_RECT_PACK_IMPLEMENTATION
#include <TrussC.h>
#include <unordered_map>
namespace tcx::imgui::detail {
namespace {
std::unordered_map<uint32_t, sg_pipeline_desc> recipes;
sg_pipeline registerRecipe(const sg_pipeline_desc* desc) {
    recipes[desc->shader.id] = *desc;
    return {}; // resolved immediately before render, never used as a GPU handle
}
// Only the active NewFrame dispatch suppresses native keyboard control.
// sokol_imgui has a cursor opt-out, but no software-keyboard opt-out.
bool suppressNativeKeyboard = false;
#if !defined(SOKOL_IMGUI_NO_SOKOL_APP)
bool keyboardShown() {
    return suppressNativeKeyboard ? false : sapp_keyboard_shown();
}
void showKeyboard(bool shown) {
    if (!suppressNativeKeyboard) sapp_show_keyboard(shown);
}
#endif
sg_pipeline appliedPipeline = {};
void applyPipeline(sg_pipeline pipeline) {
    appliedPipeline = pipeline;
    sg_apply_pipeline(pipeline);
}
void applyBindings(const sg_bindings* bindings);
void releasePipeline(sg_pipeline) {} // target cache owns the pipelines
void releaseProgram(sg_shader shader) {
    recipes.erase(shader.id);
    tc::internal::retirePipelineProgram(shader);
    tc::internal::deferGpuDestroy(shader);
}
}
}
#define sg_make_pipeline tcx::imgui::detail::registerRecipe
#define sg_destroy_pipeline tcx::imgui::detail::releasePipeline
#define sg_destroy_shader tcx::imgui::detail::releaseProgram
#define sg_apply_bindings tcx::imgui::detail::applyBindings
#define sg_apply_pipeline tcx::imgui::detail::applyPipeline
#define sapp_keyboard_shown tcx::imgui::detail::keyboardShown
#define sapp_show_keyboard tcx::imgui::detail::showKeyboard
#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"
#undef SOKOL_IMGUI_IMPL
#undef sapp_keyboard_shown
#undef sapp_show_keyboard
#undef sg_make_pipeline
#undef sg_destroy_pipeline
#undef sg_destroy_shader
#undef sg_apply_bindings
#undef sg_apply_pipeline
#include "tcImGuiTarget.h"

namespace tcx::imgui::detail {
namespace {
// The backend resets to its filtered pipeline after a user callback, even
// when the next command keeps the same unfilterable texture. Reconcile the
// sampling variant on bindings too, including that reset path.
void applyBindings(const sg_bindings* bindings) {
    const auto image = sg_query_view_image(bindings->views[0]);
    const bool filterable = sg_query_pixelformat(sg_query_image_pixelformat(image)).filter;
    const auto pipeline = filterable ? _simgui.def_pip : _simgui.pip_unfilterable;
    if (appliedPipeline.id != pipeline.id) {
        applyPipeline(pipeline);
        _simgui_vs_params_t params = {};
        params.disp_size.x = ImGui::GetIO().DisplaySize.x;
        params.disp_size.y = ImGui::GetIO().DisplaySize.y;
        sg_apply_uniforms(0, SG_RANGE_REF(params));
    }
    sg_apply_bindings(bindings);
}
struct TargetFrame {
    tc::internal::RenderTarget* target = nullptr;
    tc::internal::WindowContext* window = nullptr;
    uint64_t activation = 0;
    uint64_t gpuFrame = 0;
    uint64_t reservedFrame = UINT64_MAX;
    bool submitted = false;
    static void committed(void* data) {
        auto& frame = *static_cast<TargetFrame*>(data);
        // Another window may present before this context's onRender. Keep
        // its unfinished UI reserved across that commit; only a completed
        // render releases the slot for a new ImGui frame.
        if (frame.reservedFrame == frame.gpuFrame && !frame.submitted)
            ++frame.reservedFrame;
        ++frame.gpuFrame; // actual sg_commit boundary, never an app/target frame
    }
};
// Unique pointers keep commit-listener addresses stable across map rehashes.
std::unordered_map<void*, std::unique_ptr<TargetFrame>> frames;
}
bool beginTargetFrame(simgui_tc_context context, const simgui_frame_desc_t& desc) {
    auto& slot = frames[context.ptr];
    if (!slot) {
        slot = std::make_unique<TargetFrame>();
        if (!sg_add_commit_listener({TargetFrame::committed, slot.get()})) {
            slot.reset();
            tc::logError("ImGui") << "could not register GPU-frame reservation";
            return false;
        }
    }
    auto& frame = *slot;
    if (frame.reservedFrame == frame.gpuFrame) {
        tc::logError("ImGui") << "one render per context per Sokol frame; use a separate ImGui context for another target";
        return false;
    }
    frame.reservedFrame = frame.gpuFrame; // reserve before NewFrame, including deferred window UI
    frame.submitted = false;
    frame.target = &tc::internal::currentTarget();
    frame.window = &tc::internal::currentWindowContext();
    frame.activation = frame.target->activation;
    simgui_tc_set_context(context);
    // An Fbo frame has no native input surface, even when a window manager
    // renders it. Only the window under the pointer may set the app cursor.
    const bool savedCursorControl = _simgui.desc.disable_set_mouse_cursor;
    _simgui.desc.disable_set_mouse_cursor = savedCursorControl || frame.target->isFbo ||
        !frame.window->pointerValid || frame.window->mouseX < 0 || frame.window->mouseY < 0 ||
        frame.window->mouseX >= tc::getWindowWidth() || frame.window->mouseY >= tc::getWindowHeight();
    suppressNativeKeyboard = frame.target->isFbo;
    simgui_new_frame(&desc);
    suppressNativeKeyboard = false;
    _simgui.desc.disable_set_mouse_cursor = savedCursorControl;
    return true;
}
bool validateTargetFrame(simgui_tc_context context) {
    auto found = frames.find(context.ptr);
    if (found == frames.end() || !found->second ||
        found->second->reservedFrame != found->second->gpuFrame || found->second->submitted) return false;
    auto& frame = *found->second;
    auto& target = tc::internal::currentTarget();
    if (frame.window == &tc::internal::currentWindowContext() &&
        frame.target == &target && frame.activation == target.activation) return true;
    frame.submitted = true;
    simgui_tc_set_context(context);
    ImGui::EndFrame();
    tc::logError("ImGui") << "UI must end on the target activation where it began";
    return false;
}
void renderTargetFrame(simgui_tc_context context) {
    if (!validateTargetFrame(context)) return;
    frames.at(context.ptr)->submitted = true; // dispatch at most once
    simgui_tc_set_context(context);
    auto& target = tc::internal::currentTarget();
    tc::internal::ensureCurrentPass();
    target.flushLayers();
    auto select = [&](sg_shader shader) {
        auto desc = recipes.at(shader.id);
        if (target.isFbo) {
            desc.colors[0].write_mask = SG_COLORMASK_RGBA;
            desc.colors[0].blend.op_rgb = SG_BLENDOP_ADD;
            desc.colors[0].blend.op_alpha = SG_BLENDOP_ADD;
            desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        }
        return target.pipeline(tc::internal::PipelineKey::raw(tc::internal::PipelineRole::ImGui, desc));
    };
    const auto filtered = select(_simgui.def_shd);
    const auto unfilterable = select(_simgui.shd_unfilterable);
    _simgui.def_pip = filtered.raw;
    _simgui.pip_unfilterable = unfilterable.raw;
    if (filtered.raw.id && unfilterable.raw.id) simgui_render();
    else ImGui::EndFrame();
    // Backend applies a full-target viewport/scissor. Restore target state for
    // subsequent raw and immediate drawing without changing its effective role.
    sg_reset_state_cache();
    const auto& viewport = target.viewport;
    sg_apply_viewport(int(viewport.x), int(viewport.y),
        viewport.active ? int(viewport.w) : target.width,
        viewport.active ? int(viewport.h) : target.height, true);
    const auto& scissor = target.currentScissor;
    sg_apply_scissor_rect(scissor.active ? int(scissor.x) : 0,
        scissor.active ? int(scissor.y) : 0,
        scissor.active ? int(scissor.w) : target.width,
        scissor.active ? int(scissor.h) : target.height, true);
}
void destroyTargetContext(simgui_tc_context context) {
    auto found = frames.find(context.ptr);
    if (found != frames.end()) {
        if (found->second) sg_remove_commit_listener({TargetFrame::committed, found->second.get()});
        frames.erase(found);
    }
    const auto previous = simgui_tc_get_context();
    auto* previousImGui = ImGui::GetCurrentContext();
    simgui_tc_set_context(context);
    auto& textures = ImGui::GetPlatformIO().Textures;
    for (int i = textures.Size - 1; i >= 0; --i) {
        auto* texture = textures[i];
        if (texture->RefCount == 1 && texture->TexID == ImTextureID_Invalid) {
            // A rejected target may have ended a CPU frame without ever
            // uploading its font atlas. The backend shutdown loop assumes
            // every listed owned texture has a GPU handle. There is nothing
            // to destroy for this entry; the atlas still owns its CPU data.
            texture->SetStatus(ImTextureStatus_Destroyed);
            textures.erase(textures.begin() + i);
        }
    }
    simgui_tc_set_context(previous);
    ImGui::SetCurrentContext(previousImGui);
    simgui_tc_destroy_context(context);
}
}
