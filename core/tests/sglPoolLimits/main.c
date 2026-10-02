// =============================================================================
// core/tests/sglPoolLimits — sokol_gl pool handling in the TrussC fork
// (sokol_gl_tc.h, #317), on SOKOL_DUMMY_BACKEND (no GPU, no window).
//
// Checks:
//   (a) sgl_context_make_pipeline() is all or nothing: when the sg pipeline
//       pool cannot hold all of an sgl pipeline's sg pipelines, it returns id
//       0 and the sg pipelines it had made are destroyed again.
//   (b) _sgl_draw() skips a draw command whose sg pipeline is id 0 (recorded
//       while a destroyed sgl pipeline was loaded) and draws the others.
//   (c) the sgl context pool grows when full: more contexts than
//       context_pool_size can be made, and the current context keeps working
//       across the grow (drawing, commit rewind).
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
// failure with a nonzero FAILED handle to exercise the other rollback path.
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

static int g_growth_warnings = 0;
static unsigned g_growth_sizes = 0;
static void grow_logger(const char* tag, uint32_t level, uint32_t item,
                        const char* message, uint32_t line, const char* filename,
                        void* user_data) {
    if (item == SGL_LOGITEM_CONTEXT_POOL_GROWN && level == 2 && message) {
        ++g_growth_warnings;
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

// sg pipelines made per sgl pipeline: one per primitive type, quads share
// the triangle pipeline.
#define SG_PIPS_PER_SGL_PIP (SGL_NUM_PRIMITIVE_TYPES - 1)

// (a) Pool of 23 sg pipelines: the default context's pipeline takes 5, three
// more sgl pipelines take 15, and the fifth sgl pipeline finds 3 free slots.
static void test_pipeline_rollback(void) {
    const int pool = 23;
    sg_setup(&(sg_desc){ .pipeline_pool_size = pool, .logger.func = slog_func });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    check("(c) default context pool starts at 8", _sgl.context_pool.pool.size == 9);

    // Room for 3 more sgl pipelines; the 4th one does not fit.
    sgl_pipeline made[4] = {{0}};
    int n_made = 0;
    bool got_failure = false;
    for (int i = 0; i < 4; i++) {
        sgl_pipeline p = sgl_context_make_pipeline(sgl_default_context(),
                                                   &(sg_pipeline_desc){0});
        if (p.id == SG_INVALID_ID) { got_failure = true; break; }
        made[n_made++] = p;
    }
    const uint32_t alive = sg_pipelines_alive();
    const uint32_t expect_alive = (uint32_t)(4 * SG_PIPS_PER_SGL_PIP);
    printf("  [rollback] sgl pipelines made=%d (want 3)  sg pipelines alive=%u (want %u of %d)\n",
           n_made, alive, expect_alive, pool);
    check("(a) sgl pipeline that does not fit returns id 0",
          got_failure && n_made == 3);
    check("(a) its partly made sg pipelines are destroyed again",
          alive == expect_alive);

    // The free slots really are free: plain sg pipelines fit into them.
    int extra = 0;
    for (int i = 0; i < pool; i++) {
        sg_pipeline_desc d = {0};
        d.shader = _sgl.shd;
        d.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        d.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        d.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
        d.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT;
        sg_pipeline p = sg_make_pipeline(&d);
        if (p.id == SG_INVALID_ID) break;
        ++extra;
    }
    printf("  [rollback] plain sg pipelines made afterwards=%d (want %d)\n",
           extra, pool - (int)expect_alive);
    check("(a) the freed sg pipeline slots can be used again",
          extra == pool - (int)expect_alive);

    // Destroying an sgl pipeline returns its sg pipelines.
    const uint32_t before = sg_pipelines_alive();
    sgl_destroy_pipeline(made[0]);
    check("(a) sgl_destroy_pipeline() frees its sg pipelines",
          sg_pipelines_alive() == before - SG_PIPS_PER_SGL_PIP);

    sgl_shutdown();
    sg_shutdown();
}

static void test_failed_pipeline_rollback(void) {
    for (int fail = 1; fail <= SG_PIPS_PER_SGL_PIP; ++fail) {
        sg_setup(&(sg_desc){ .logger.func = slog_func });
        // Only the default pipeline and one more sgl slot fit. A successful
        // creation after rollback proves that the failed sgl slot was freed.
        sgl_setup(&(sgl_desc_t){ .pipeline_pool_size = 2, .logger.func = slog_func });
        const uint32_t before = sg_pipelines_alive();
        g_fail_pipeline_countdown = fail;
        sgl_pipeline failed = sgl_make_pipeline(&(sg_pipeline_desc){0});
        printf("  [backend failure] failing sg pipeline %d of %d\n", fail, SG_PIPS_PER_SGL_PIP);
        check("(a) nonzero FAILED sg pipeline rolls back and returns id 0",
              failed.id == SG_INVALID_ID && sg_pipelines_alive() == before);
        sgl_pipeline next = sgl_make_pipeline(&(sg_pipeline_desc){0});
        check("(a) failed sgl slot can be reused after backend failure",
              next.id != SG_INVALID_ID && sg_pipelines_alive() == before + SG_PIPS_PER_SGL_PIP);
        sgl_shutdown();
        sg_shutdown();
    }
}

// (c) context_pool_size = 2 (the default context plus one).
static void test_context_pool_grow(void) {
    sg_setup(&(sg_desc){ .logger.func = slog_func });
    g_growth_warnings = 0;
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
    check("(c) each grow warns with the new size (also in Release)",
          g_growth_warnings == 2 && g_growth_sizes == 3);
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

int main(void) {
    test_pipeline_rollback();
    test_failed_pipeline_rollback();
    test_context_pool_grow();
    test_context_pool_grow_failure();
    test_skip_invalid_pipeline();   // last: a draw with pipeline 0 aborts in sokol

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
