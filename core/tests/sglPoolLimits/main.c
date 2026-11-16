// =============================================================================
// core/tests/sglPoolLimits — sokol_gl pool handling in the TrussC fork
// (sokol_gl_tc.h, #317), on SOKOL_DUMMY_BACKEND (no GPU, no window).
//
// Checks:
//   (a) primitive pipelines are lazy; a failed child skips only its draw,
//       leaves other children usable, and retries on its next use.
//   (b) _sgl_draw() skips a draw command whose sg pipeline is id 0 (recorded
//       while a destroyed sgl pipeline was loaded) and draws the others.
//   (c) the sgl context pool grows when full: more contexts than
//       context_pool_size can be made, and the current context keeps working
//       across the grow (drawing, commit rewind).
//   (d) GPU flush buffers grow even when the sg buffer pool is full, and
//       normal drawing continues on the next frame.
//
// Standalone target (not a TrussC project): it needs its own SOKOL_IMPL with
// the dummy backend, which would clash with libTrussC's sokol implementation.
// Console output, exit code = pass/fail (build_all.py runs it under
// --core-tests-only).
// =============================================================================

#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include "sokol_log.h"
#include "sokol_gfx.h"

// The dummy backend normally always succeeds. Inject a backend creation
// failure with a nonzero FAILED handle to exercise the nonzero-handle failure path.
static int g_fail_pipeline_countdown = 0;
static sg_pipeline test_make_pipeline(const sg_pipeline_desc* desc) {
    if (g_fail_pipeline_countdown > 0 && --g_fail_pipeline_countdown == 0) {
        sg_pipeline failed = sg_alloc_pipeline();
        sg_fail_pipeline(failed);
        return failed;
    }
    return sg_make_pipeline(desc);
}
#define sg_make_pipeline test_make_pipeline
#include "util/sokol_gl_tc.h"
#undef sg_make_pipeline

#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static int g_growth_notices = 0;
static unsigned g_growth_sizes = 0;
static void grow_logger(const char* tag, uint32_t level, uint32_t item,
                        const char* message, uint32_t line, const char* filename,
                        void* user_data) {
    if (item == SGL_LOGITEM_CONTEXT_POOL_GROWN && level == 3 && message) {
        ++g_growth_notices;
        if (strstr(message, "2 -> 4 contexts")) g_growth_sizes |= 1;
        if (strstr(message, "4 -> 8 contexts")) g_growth_sizes |= 2;
    }
    slog_func(tag, level, item, message, line, filename, user_data);
}

static uint32_t sg_pipelines_alive(void) {
    return sg_query_stats().total.pipelines.alive;
}

static void push_tri(void) {
    sgl_begin_triangles();
    sgl_v2f(0.0f, 0.0f);
    sgl_v2f(1.0f, 0.0f);
    sgl_v2f(0.0f, 1.0f);
    sgl_end();
}

static int pipeline_errors;
static void pipeline_logger(const char* tag, uint32_t level, uint32_t item,
                            const char* message, uint32_t line, const char* file, void* user) {
    if (strcmp(tag, "sgl") == 0 && item == SGL_LOGITEM_MAKE_PIPELINE_FAILED && level == 1)
        ++pipeline_errors;
    slog_func(tag, level, item, message, line, file, user);
}

static void push_points(void) {
    sgl_begin_points(); sgl_v2f(0, 0); sgl_end();
}
static void push_lines(void) {
    sgl_begin_lines(); sgl_v2f(0, 0); sgl_v2f(1, 1); sgl_end();
}
static void begin_pass(void) {
    sg_begin_pass(&(sg_pass){ .swapchain = { .width = 16, .height = 16,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1 } });
}

static void test_lazy_primitives(void) {
    sg_setup(&(sg_desc){ .logger.func = slog_func });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    sg_enable_stats();
    check("(a) default context creates no sg pipelines", sg_pipelines_alive() == 0);
    sgl_pipeline unused = sgl_context_make_pipeline(sgl_default_context(), &(sg_pipeline_desc){0});
    sgl_destroy_pipeline(unused);
    check("(a) unused pipeline destroys safely", sg_pipelines_alive() == 0);
    // The descriptor and label may be temporary caller storage.
    char label[] = "temporary-label";
    sg_pipeline_desc desc = { .label = label };
    sgl_pipeline p = sgl_make_pipeline(&desc);
    label[0] = 'X'; desc.colors[0].write_mask = SG_COLORMASK_NONE;
    sgl_load_pipeline(p);
    push_tri(); push_tri();
    check("(a) triangle-only pipeline creates exactly one child", sg_pipelines_alive() == 1);
    sg_pipeline triangles = _sgl_get_pipeline(p, SGL_PRIMITIVETYPE_TRIANGLES);
    check("(a) descriptor snapshot preserves caller render state", sg_query_pipeline_desc(triangles).colors[0].write_mask == SG_COLORMASK_RGB);
    check("(a) descriptor snapshot owns its label", strcmp(_sgl_lookup_pipeline(p.id)->desc.label, "temporary-label") == 0);
    sgl_begin_quads();
    for (int i = 0; i < 4; ++i) sgl_v2f(0, 0);
    sgl_end();
    check("(a) quads share the triangle child", sg_pipelines_alive() == 1 &&
        _sgl_get_pipeline(p, SGL_PRIMITIVETYPE_QUADS).id == triangles.id);
    push_points();
    check("(a) points create one child on first use", sg_pipelines_alive() == 2);
    push_lines(); push_lines();
    check("(a) lines create once on first use", sg_pipelines_alive() == 3);
    sgl_begin_line_strip(); sgl_v2f(0, 0); sgl_v2f(1, 1); sgl_end();
    sgl_begin_triangle_strip();
    sgl_v2f(0, 0); sgl_v2f(1, 0); sgl_v2f(0, 1); sgl_end();
    check("(a) strips create distinct children", sg_pipelines_alive() == 5);
    begin_pass(); sgl_draw();
    check("(a) every primitive draws, repeated compatible draws merge", sg_query_stats().cur_frame.num_draw == 5);
    sg_end_pass(); sg_commit();
    sgl_destroy_pipeline(p);
    check("(a) destroy frees each child exactly once", sg_pipelines_alive() == 0);
    p = sgl_make_pipeline(&(sg_pipeline_desc){0});
    sgl_load_pipeline(p);
    sgl_begin_quads();
    for (int i = 0; i < 4; ++i) sgl_v2f(0, 0);
    sgl_end();
    check("(a) quads-first creates exactly the triangle child", sg_pipelines_alive() == 1 &&
        _sgl_get_pipeline(p, SGL_PRIMITIVETYPE_QUADS).id == _sgl_get_pipeline(p, SGL_PRIMITIVETYPE_TRIANGLES).id);
    sg_commit();
    sgl_destroy_pipeline(p);
    sgl_shutdown(); sg_shutdown();
}

static void test_child_failure(bool backend_failure) {
    pipeline_errors = 0;
    sg_setup(&(sg_desc){ .pipeline_pool_size = backend_failure ? 8 : 1, .logger.func = slog_func });
    sgl_setup(&(sgl_desc_t){ .logger.func = pipeline_logger });
    sg_enable_stats();
    sgl_pipeline p = sgl_make_pipeline(&(sg_pipeline_desc){0});
    sgl_load_pipeline(p); push_tri();
    if (backend_failure) {
        g_fail_pipeline_countdown = 1;
        sgl_translate(2, 3, 0);
    }
    push_points();
    if (backend_failure) check("(a) failed child preserves dirty matrices", _sgl.cur_ctx->matrix_dirty);
    check("(a) failed child reports the existing error", pipeline_errors == 1);
    check("(a) failed child leaves parent and siblings valid", sg_pipelines_alive() == 1 &&
        _sgl_lookup_pipeline(p.id)->slot.state == SG_RESOURCESTATE_VALID && !sgl_error().any);
    // A failed point draw between two triangles must neither record nor merge
    // its vertices into the previous triangle command.
    push_tri();
    check("(a) skipped vertices cannot enter a merged triangle draw", sgl_num_vertices() == 6);
    begin_pass(); sgl_draw();
    check("(a) failure skips only its draw; triangles still draw", sg_query_stats().cur_frame.num_draw == (backend_failure ? 2u : 1u) &&
        _sgl.cur_ctx->commands.ptr[0].args.draw.num_vertices == (backend_failure ? 3 : 6));
    sg_end_pass(); sg_commit();
    if (!backend_failure) {
        // Release the triangle child by destroying its owner, making room for
        // the failed point child of another parent below.
        sgl_pipeline other = sgl_make_pipeline(&(sg_pipeline_desc){0});
        sgl_load_pipeline(other); push_points();
        check("(a) full pool still skips points", pipeline_errors == 2);
        sgl_destroy_pipeline(p); p = other;
    }
    push_points();
    check("(a) later use retries the failed child without leaking", sg_pipelines_alive() == (backend_failure ? 2u : 1u));
    begin_pass(); sgl_draw();
    check("(a) retried child draws", sg_query_stats().cur_frame.num_draw == 1);
    sg_end_pass(); sg_commit();
    sgl_destroy_pipeline(p);
    check("(a) partial children destroy without leaks", sg_pipelines_alive() == 0);
    sgl_shutdown(); sg_shutdown();
}

// (c) context_pool_size = 2 (the default context plus one).
static void test_context_pool_grow(void) {
    sg_setup(&(sg_desc){ .logger.func = slog_func });
    g_growth_notices = 0;
    g_growth_sizes = 0;
    sgl_setup(&(sgl_desc_t){ .context_pool_size = 2, .logger.func = grow_logger });

    sgl_context_desc_t cd = { .max_vertices = 64, .max_commands = 16 };
    sgl_context a = sgl_make_context(&cd);
    sgl_set_context(a);
    push_tri();                                   // recorded before the grow
    sgl_context more[3];
    bool all_valid = (a.id != SG_INVALID_ID);
    for (int i = 0; i < 3; i++) {                 // the first one grows 2 -> 4
        more[i] = sgl_make_context(&cd);
        if (more[i].id == SG_INVALID_ID) all_valid = false;
    }
    check("(c) 4 contexts besides the default with a pool of 2 (grows)", all_valid);
    check("(c) each grow reports info with the new size (also in Release)",
          g_growth_notices == 2 && g_growth_sizes == 3);
    check("(c) current context still set after the grow",
          a.id != SG_INVALID_ID && sgl_get_context().id == a.id && !sgl_error().no_context);
    push_tri();
    printf("  [grow] current context vertices=%d (want 6)\n", sgl_num_vertices());
    check("(c) current context keeps its recorded vertices",
          sgl_num_vertices() == 6);
    sg_enable_stats();
    sg_begin_pass(&(sg_pass){ .swapchain = { .width = 16, .height = 16,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1 } });
    sgl_draw();
    check("(c) recorded triangles draw after the context moved",
          sg_query_stats().cur_frame.num_draw == 1);
    sg_end_pass();
    sg_commit();                                  // commit listener rewinds by id
    check("(c) commit rewinds the moved context",
          a.id != SG_INVALID_ID && sgl_num_vertices() == 0);

    sgl_destroy_context(more[1]);
    sgl_context again = sgl_make_context(&cd);
    check("(c) a freed slot is reused", again.id != SG_INVALID_ID);
    sgl_shutdown();
    sg_shutdown();
}

typedef struct {
    int calls;
    int fail_call;
    int alive;
} test_allocator;

static void* test_alloc(size_t size, void* user_data) {
    test_allocator* a = (test_allocator*)user_data;
    if (++a->calls == a->fail_call) return NULL;
    void* ptr = malloc(size);
    if (ptr) ++a->alive;
    return ptr;
}

static void test_free(void* ptr, void* user_data) {
    test_allocator* a = (test_allocator*)user_data;
    if (ptr) --a->alive;
    free(ptr);
}

// Every allocation in the grow can fail independently. Nothing in the old
// pool may change, and none of the temporary allocations may leak.
static void test_context_pool_grow_failure(void) {
    for (int fail = 1; fail <= 3; ++fail) {
        test_allocator alloc = {0};
        sg_setup(&(sg_desc){ .logger.func = slog_func });
        sgl_setup(&(sgl_desc_t){ .context_pool_size = 2, .logger.func = slog_func,
            .allocator = { .alloc_fn = test_alloc, .free_fn = test_free,
                           .user_data = &alloc } });
        sgl_context_desc_t cd = { .max_vertices = 64, .max_commands = 16 };
        sgl_context a = sgl_make_context(&cd);
        sgl_set_context(a);
        push_tri();
        const int alive = alloc.alive;
        alloc.fail_call = alloc.calls + fail;
        printf("  [grow failure] failing allocation %d of 3\n", fail);
        sgl_context failed = sgl_make_context(&cd);
        check("(c) failed grow returns id 0 without leaking allocations",
              failed.id == SG_INVALID_ID && alloc.alive == alive);
        check("(c) failed grow preserves the current context and vertices",
              sgl_get_context().id == a.id && sgl_num_vertices() == 3);
        sg_commit();
        check("(c) commit still rewinds after a failed grow",
              sgl_num_vertices() == 0);

        // The raw sgl API can grow later; TrussC's target caches the failure.
        alloc.fail_call = 0;
        sgl_context next = sgl_make_context(&cd);
        check("(c) pool remains usable after a failed grow",
              next.id != SG_INVALID_ID && sgl_get_context().id == a.id);
        sgl_shutdown();
        check("(c) shutdown frees all allocations after a failed grow",
              alloc.alive == 0);
        sg_shutdown();
    }
}

// (b) A command recorded while a destroyed sgl pipeline is loaded is skipped.
static void test_skip_invalid_pipeline(void) {
    sg_setup(&(sg_desc){ .logger.func = slog_func });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    sg_enable_stats();

    sgl_pipeline good = sgl_make_pipeline(&(sg_pipeline_desc){0});
    sgl_pipeline gone = sgl_make_pipeline(&(sg_pipeline_desc){0});
    sgl_destroy_pipeline(gone);

    sgl_load_pipeline(good);
    push_tri();
    sgl_load_pipeline(gone);
    push_tri();

    sg_begin_pass(&(sg_pass){ .swapchain = { .width = 16, .height = 16,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1 } });
    sgl_draw();
    const uint32_t draws = sg_query_stats().cur_frame.num_draw;
    sg_end_pass();
    printf("  [skip] sg_draw calls=%u (want 1)\n", draws);
    check("(b) command with sg pipeline 0 is skipped, the other drawn",
          draws == 1);
    sg_commit();

    sgl_shutdown();
    sg_shutdown();
}

// (d) The initial sgl buffer plus three plain buffers fill the sg pool.
static void test_full_buffer_pool_growth(void) {
    sg_setup(&(sg_desc){ .buffer_pool_size = 4, .logger.func = slog_func });
    sgl_setup(&(sgl_desc_t){ .max_vertices = 3, .logger.func = slog_func });
    sg_enable_stats();
    _sgl_context_t* ctx = _sgl_lookup_context(sgl_default_context().id);
    const sg_buffer initial = ctx->vbuf;
    sg_buffer fillers[3];
    for (int i = 0; i < 3; ++i) {
        fillers[i] = sg_make_buffer(&(sg_buffer_desc){
            .size = 16, .usage.vertex_buffer = true, .usage.write_transient = true });
        check("(d) filler buffer is valid",
              sg_query_buffer_state(fillers[i]) == SG_RESOURCESTATE_VALID);
    }
    check("(d) sg buffer pool is full", sg_query_stats().total.buffers.alive == 4);

    for (int frame = 0; frame < 2; ++frame) {
        push_tri();
        if (frame == 0) push_tri();               // six vertices outgrow the initial three
        sg_begin_pass(&(sg_pass){ .swapchain = { .width = 16, .height = 16,
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1 } });
        sgl_draw();
        const sgl_error_t error = sgl_error();
        check(frame == 0 ? "(d) frame N grows a flush buffer with a full sg pool"
                         : "(d) frame N+1 draws normally with a valid flush buffer",
              !error.any && !error.vertices_full && ctx->flush_count == 1 &&
              sg_query_buffer_state(ctx->flush_buffers[0]) == SG_RESOURCESTATE_VALID &&
              ctx->vbuf.id == ctx->flush_buffers[0].id &&
              ctx->bind.vertex_buffers[0].id == ctx->vbuf.id &&
              sg_query_stats().cur_frame.num_draw == 1);
        sg_end_pass();
        sg_commit();
    }
    check("(d) initial buffer was replaced with sufficient capacity",
          sg_query_buffer_state(initial) == SG_RESOURCESTATE_INVALID &&
          sg_query_buffer_size(ctx->vbuf) >= 6 * sizeof(_sgl_vertex_t));
    sgl_shutdown();
    check("(d) shutdown frees only sgl's buffer",
          sg_query_stats().total.buffers.alive == 3);
    for (int i = 0; i < 3; ++i) sg_destroy_buffer(fillers[i]);
    sg_shutdown();
}

int main(void) {
    test_lazy_primitives();
    test_child_failure(false);
    test_child_failure(true);
    test_context_pool_grow();
    test_context_pool_grow_failure();
    test_full_buffer_pool_growth();
    test_skip_invalid_pipeline();   // last: a draw with pipeline 0 aborts in sokol

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
