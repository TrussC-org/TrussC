// A retained recording must not let a dead texture suppress healthy draws on
// this or later frames. Compile the production renderer with the dummy backend.
#define SOKOL_IMPL
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL
#define SOKOL_TRACE_HOOKS
#define SOKOL_DUMMY_BACKEND
#include "sokol_gfx.h"
#include "util/sokol_gl_tc.h"
#include <stdio.h>

static int failures, validDraws, errors;
static void check(const char* name, bool ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
static void logger(const char* tag, uint32_t level, uint32_t item, const char* msg,
                   uint32_t line, const char* file, void* user) {
    (void)tag; (void)item; (void)msg; (void)line; (void)file; (void)user;
    if (level <= 1) ++errors;
}
static void drawn(int base, int count, int instances, void* user) {
    (void)base; (void)count; (void)instances; (void)user;
    if (_sg.next_draw_valid) ++validDraws;
}
static void triangle(sg_view view) {
    sgl_enable_texture();
    sgl_texture(view, _sgl.def_smp);
    sgl_begin_triangles();
    sgl_v2f(0, 0); sgl_v2f(1, 0); sgl_v2f(0, 1);
    sgl_end();
}
int main(void) {
    sg_setup(&(sg_desc){ .logger.func = logger });
    sgl_setup(&(sgl_desc_t){ .logger.func = logger });
    sg_trace_hooks hooks = { .draw = drawn };
    sg_install_trace_hooks(&hooks);
    const sgl_context ctx = sgl_get_context();
    sgl_tc_context_target_owned(ctx);
    const uint32_t white = 0xffffffff;
    const sg_image_desc desc = { .width = 1, .height = 1, .data = SG_RANGE(white) };
    for (int destroyView = 0; destroyView < 2; ++destroyView) {
        const sg_image image = sg_make_image(&desc);
        const sg_view view = sg_make_view(&(sg_view_desc){ .texture.image = image });
        triangle(view);
        triangle(_sgl.def_view); // Same pipeline; must recover after the bad draw.
        if (destroyView) sg_destroy_view(view);
        sg_destroy_image(image);
        for (int frame = 0; frame < 4; ++frame) {
            const sg_pass pass = { .swapchain = {
                .width = 32, .height = 32, .sample_count = 1,
                .color_format = SG_PIXELFORMAT_RGBA8,
                .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL } };
            validDraws = 0;
            sg_begin_pass(&pass);
            sgl_draw();
            check("healthy draw survives dead atlas in retained recording", validDraws == 1);
            sg_end_pass();
            sg_commit(); // The target owns completion; replay again next frame.
        }
        sgl_tc_context_complete(ctx);
        if (!destroyView) sg_destroy_view(view);
    }
    check("dead texture draws never reach sokol validation", errors == 0);
    sgl_shutdown();
    sg_shutdown();
    return failures ? 1 : 0;
}
