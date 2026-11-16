// Standalone dummy-backend unit test; assertion interception proves omissions
// are visible without deliberately crashing the test process.
// Keep sokol's debug validation in Release builds too.
#undef NDEBUG
#include <stdio.h>
static int assertions;
#define SOKOL_ASSERT(condition) ((condition) ? (void)0 : (void)++assertions)
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include "sokol_gfx.h"
static unsigned ref_calls;
static void tracked_stencil_ref(uint8_t ref) {
    ++ref_calls;
    sg_apply_stencil_ref(ref);
}
static uint8_t replay_refs[16];
static unsigned replay_count;
static void tracked_draw(int base, int count, int instances) {
    if (replay_count < 16) replay_refs[replay_count++] = _sg.stencil_ref;
    sg_draw(base, count, instances);
}
#define sg_draw tracked_draw
#define sg_apply_stencil_ref tracked_stencil_ref
#include "util/sokol_gl_tc.h"
#undef sg_apply_stencil_ref
#undef sg_draw

static int failures;
static void check(const char* label, bool ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static void triangle(void) {
    sgl_begin_triangles();
    sgl_v2f(0, 0); sgl_v2f(1, 0); sgl_v2f(0, 1);
    sgl_end();
}
static void begin_pass(void) {
    sg_begin_pass(&(sg_pass){ .swapchain = { .width = 16, .height = 16,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1 } });
}
static void mixed_pipelines(bool framework_first) {
    sgl_context context = sgl_make_context(&(sgl_context_desc_t){0});
    sgl_set_context(context);
    sg_pipeline_desc desc = {0};
    desc.stencil.enabled = true;
    desc.stencil._tc_dynamic_ref = true;
    sgl_pipeline framework = sgl_make_pipeline(&desc);
    desc.stencil._tc_dynamic_ref = false;
    desc.stencil.ref = 61;
    sgl_pipeline user = sgl_make_pipeline(&desc);
    // Mirror framework loads even when the surrounding framework ref is sticky.
    sgl_load_pipeline(framework);
    sgl_tc_stencil_ref(17);
    sgl_load_pipeline(framework_first ? framework : user);
    triangle();
    sgl_push_pipeline();
    sgl_load_pipeline(framework_first ? user : framework);
    triangle();
    sgl_pop_pipeline();
    triangle();
    const uint8_t expected[] = {framework_first ? 17 : 61,
        framework_first ? 61 : 17, framework_first ? 17 : 61};
    bool captured = _sgl.cur_ctx->commands.next == 3;
    for (int i = 0; i < 3 && captured; ++i) {
        const _sgl_draw_args_t* draw = &_sgl.cur_ctx->commands.ptr[i].args.draw;
        captured &= draw->stencil_ref == expected[i] &&
            draw->stencil_ref_override == (expected[i] == 17);
    }
    check(framework_first ? "framework/user/pop captures 17/61/17" :
        "user/framework/pop captures 61/17/61", captured);
    ref_calls = replay_count = 0;
    const int before = assertions;
    begin_pass();
    sgl_draw();
    bool replayed = replay_count == 3 && assertions == before;
    for (int i = 0; i < 3 && replayed; ++i) replayed &= replay_refs[i] == expected[i];
    check(framework_first ? "framework/user/pop replays 17/61/17" :
        "user/framework/pop replays 61/17/61", replayed);
    check("mixed baked pipelines never dispatch dynamic refs", ref_calls == (framework_first ? 2u : 1u));
    sg_end_pass();
    sg_commit();
    sgl_destroy_pipeline(user);
    sgl_destroy_pipeline(framework);
    sgl_destroy_context(context);
}
int main(void) {
    sg_setup(&(sg_desc){0});
    sgl_setup(&(sgl_desc_t){0});
    sg_enable_stats();
    sg_pipeline_desc desc = {0};
    desc.stencil.enabled = true;
    desc.stencil._tc_dynamic_ref = true;
    sgl_pipeline pipeline = sgl_make_pipeline(&desc);
    sgl_load_pipeline(pipeline);
    sgl_tc_stencil_ref(17); triangle(); triangle();
    sgl_translate(1, 0, 0); triangle(); // Split matrices with the same reference.
    sgl_push_pipeline();
    sgl_tc_stencil_ref(231); triangle();
    sgl_pop_pipeline();
    sgl_tc_stencil_ref(17); triangle();
    _sgl_context_t* ctx = _sgl.cur_ctx;
    check("same refs merge; changes split even on one lazy child", ctx->commands.next == 4);
    check("values frozen at record time", ctx->commands.ptr[0].args.draw.stencil_ref == 17 &&
        ctx->commands.ptr[2].args.draw.stencil_ref == 231 && ctx->commands.ptr[3].args.draw.stencil_ref == 17);
    check("one lazy primitive pipeline", sg_query_stats().total.pipelines.alive == 1);
    sgl_tc_stencil_ref(99); // Must not affect the recorded references.
    begin_pass(); sgl_draw();
    check("replay applies every changed recorded value", _sg.stencil_ref == 17 &&
        sg_query_stats().cur_frame.num_set_stencil_ref == 3 && ref_calls == 3 && assertions == 0);
    // A raw submission disturbs the ref; the next layer replay must restore it.
    sg_pipeline child = ctx->commands.ptr[0].args.draw.pip;
    sg_apply_pipeline(child); sg_apply_stencil_ref(42);
    sgl_draw();
    check("raw draws cannot poison subsequent replay", _sg.stencil_ref == 17 && assertions == 0);
    sg_end_pass(); begin_pass(); sgl_draw();
    check("new pass restores recorded state", _sg.stencil_ref == 17 && assertions == 0);
    // Equal values need no backend dispatch, but acknowledge the debug contract.
    sg_apply_pipeline(child);
    uint32_t count = sg_query_stats().cur_frame.num_set_stencil_ref;
    sg_apply_stencil_ref(0); sg_apply_stencil_ref(0);
    check("equal applied reference skips backend work", _sg.stencil_ref_applied &&
        sg_query_stats().cur_frame.num_set_stencil_ref == count);
    sg_apply_pipeline(child);
    int before = assertions;
    _sg_draw(0, 3, 1, 0, 0);
    check("debug assertion catches a missing reference", assertions == before + 1);
    sg_apply_stencil_ref(7);
    before = assertions;
    _sg_draw(0, 3, 1, 0, 0);
    check("explicit reference satisfies debug contract", assertions == before);
    sg_end_pass(); sg_commit();
    sgl_destroy_pipeline(pipeline);
    // A fresh direct sgl context uses baked refs, including stack push/pop.
    sgl_context other = sgl_make_context(&(sgl_context_desc_t){0});
    sgl_set_context(other);
    desc.stencil._tc_dynamic_ref = false;
    desc.stencil.ref = 61;
    pipeline = sgl_make_pipeline(&desc); sgl_load_pipeline(pipeline);
    triangle(); sgl_push_pipeline(); sgl_load_default_pipeline(); triangle(); sgl_pop_pipeline(); triangle();
    check("direct callers retain immutable pipeline refs", _sgl.cur_ctx->commands.ptr[0].args.draw.stencil_ref == 61 &&
        _sgl.cur_ctx->commands.ptr[2].args.draw.stencil_ref == 61);
    ref_calls = 0;
    begin_pass(); sgl_draw();
    check("baked refs need no dynamic dispatch", sg_query_stats().cur_frame.num_set_stencil_ref == 0 && ref_calls == 0);
    sg_end_pass(); sg_commit();
    sgl_destroy_pipeline(pipeline); sgl_destroy_context(other);
    mixed_pipelines(true);
    mixed_pipelines(false);
    sgl_shutdown(); sg_shutdown();
    return failures ? 1 : 0;
}
