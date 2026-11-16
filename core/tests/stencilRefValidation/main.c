// Standalone sokol unit test: no TrussC runtime or GPU.
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include "sokol_gfx.h"
#include <stdio.h>
static int failures, errors;
static void check(const char* label, bool ok) {
    printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
static void logger(const char* tag, uint32_t level, uint32_t item,
    const char* message, uint32_t line, const char* file, void* user) {
    (void)tag; (void)message; (void)line; (void)file; (void)user;
    if (level == 1 && (item == SG_LOGITEM_VALIDATE_ASTREF_RENDERPASS_EXPECTED ||
        item == SG_LOGITEM_VALIDATE_ASTREF_NO_PIPELINE)) ++errors;
    else if (level <= 1) {
        printf("unexpected sokol diagnostic: %u\n", item);
        ++failures;
    }
}
static void expect_invalid(const char* label) {
    int before = errors;
    sg_apply_stencil_ref(3);
#ifdef SOKOL_DEBUG
    check(label, errors > before);
#else
    check(label, errors == before);
#endif
}
int main(void) {
    sg_desc setup = {0};
    setup.logger.func = logger;
    sg_setup(&setup);
    expect_invalid("outside pass: debug error / release no-op");
    sg_pass pass = {0};
    pass.swapchain.width = pass.swapchain.height = 16;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pass.swapchain.sample_count = 1;
    sg_begin_pass(&pass);
    expect_invalid("no pipeline: debug error / release no-op");
    sg_shader_desc shd = {0};
    sg_shader shader = sg_make_shader(&shd);
    sg_pipeline_desc desc = {0};
    desc.shader = shader;
    desc.stencil.enabled = true;
    desc.stencil.ref = 2;
    sg_pipeline pipeline = sg_make_pipeline(&desc);
    check("dummy pipeline valid", sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID);
    sg_apply_pipeline(pipeline);
    int before = errors;
    sg_apply_stencil_ref(3);
    check("valid dummy override accepted", errors == before);
    check("pipeline's own ref stays immutable", sg_query_pipeline_desc(pipeline).stencil.ref == 2);
    sg_destroy_pipeline(pipeline);
    expect_invalid("destroyed pipeline: debug error / release no-op");
    sg_end_pass();
    sg_begin_pass(&pass);
    expect_invalid("next pass requires a pipeline again");
    desc.stencil.enabled = false;
    pipeline = sg_make_pipeline(&desc);
    sg_apply_pipeline(pipeline);
    before = errors;
    sg_apply_stencil_ref(255);
    check("disabled stencil accepted as no-op", errors == before);
    sg_end_pass();
    sg_destroy_pipeline(pipeline);
    sg_destroy_shader(shader);
    sg_shutdown();
    return failures ? 1 : 0;
}
