// #331: C11, dummy backend, no runtime or display.
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include "sokol_gfx.h"
#include "util/sokol_gl_tc.h"
#include <stdio.h>

static int failures;
static int allocations;
static bool fail_alloc;
static void* tracked_alloc(size_t size, void* userdata) {
    (void)userdata;
    if (fail_alloc) return 0;
    void* ptr = malloc(size);
    if (ptr) ++allocations;
    return ptr;
}
static void tracked_free(void* ptr, void* userdata) {
    (void)userdata;
    if (ptr) --allocations;
    free(ptr);
}
static void check(const char* label, bool ok) {
    printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
static void triangle(void) {
    sgl_begin_triangles();
    sgl_v2f(0, 0); sgl_v2f(1, 0); sgl_v2f(0, 1);
    sgl_end();
}
static void mode(int index) {
    if (index == SGL_MATRIXMODE_MODELVIEW) sgl_matrix_mode_modelview();
    else if (index == SGL_MATRIXMODE_PROJECTION) sgl_matrix_mode_projection();
    else sgl_matrix_mode_texture();
}
static void exercise(sgl_context context) {
    sgl_set_context(context);
    _sgl_context_t* ctx = _sgl_lookup_context(context.id);
    for (int m = 0; m < SGL_NUM_MATRIXMODES; ++m) {
        mode(m);
        for (int depth = 0; depth < 200; ++depth) {
            sgl_push_matrix();
            sgl_translate(1, 0, 0);
        }
        check("200 pushes succeed", !sgl_context_error(context).any);
        check("deep transform preserved", _sgl_matrix(ctx)->v[3][0] == 200);
    }
    triangle();
    check("deep draw recorded", ctx->commands.next == 1 && ctx->vertices.next == 3);
    check("deep draw captures modelview/projection/texture transforms",
          ctx->uniforms.next == 1 && ctx->uniforms.ptr[0].mvp.v[3][0] == 400 &&
          ctx->uniforms.ptr[0].tm.v[3][0] == 200);
    bool restored = true;
    for (int m = 0; m < SGL_NUM_MATRIXMODES; ++m) {
        mode(m);
        for (int depth = 200; depth > 0; --depth) {
            restored &= _sgl_matrix(ctx)->v[3][0] == depth;
            sgl_pop_matrix();
        }
        restored &= ctx->matrix_tos[m] == 0 && _sgl_matrix(ctx)->v[3][0] == 0;
    }
    check("all pops restore each saved transform and base", restored);
    triangle();
    check("draw after recursion recorded", ctx->commands.next == 2 && ctx->vertices.next == 6);
    check("draw after recursion captures restored transforms",
          ctx->uniforms.next == 2 && ctx->uniforms.ptr[1].mvp.v[3][0] == 0 &&
          ctx->uniforms.ptr[1].tm.v[3][0] == 0);
    check("no context error after recursion", !sgl_context_error(context).any);
    sg_commit();
    sgl_pop_matrix();
    check("raw underflow still reported", sgl_context_error(context).stack_underflow);
    sgl_tc_context_complete(context);
    check("target completion clears underflow", !sgl_context_error(context).any);
}
int main(void) {
    sg_setup(&(sg_desc){0});
    sgl_setup(&(sgl_desc_t){ .allocator = { tracked_alloc, tracked_free, 0 } });
    exercise(sgl_default_context());
    const int default_allocations = allocations;
    sgl_context second = sgl_make_context(&(sgl_context_desc_t){0});
    sgl_tc_context_target_owned(second);
    exercise(second);
    sgl_destroy_context(second);
    check("second context frees its matrix arrays", allocations == default_allocations);
    sgl_set_context(sgl_default_context());
    // Force a grow allocation failure without discarding the base matrix.
    _sgl_context_t* ctx = _sgl.cur_ctx;
    for (int i = 1; i < ctx->matrix_cap[ctx->cur_matrix_mode]; ++i) sgl_push_matrix();
    const int saved_top = ctx->matrix_tos[ctx->cur_matrix_mode];
    fail_alloc = true;
    sgl_push_matrix();
    fail_alloc = false;
    check("OOM reports error and preserves stack", sgl_error().stack_overflow &&
          ctx->matrix_tos[ctx->cur_matrix_mode] == saved_top);
    sgl_shutdown();
    check("shutdown frees all configured allocations", allocations == 0);
    sg_shutdown();
    return failures ? 1 : 0;
}
