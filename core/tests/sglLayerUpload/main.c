// =============================================================================
// core/tests/sglLayerUpload — regression test for the sokol_gl "per-layer full
// re-append" GPU vertex-buffer blow-up (fix/buffer-increase-bug).
//
// THE BUG: flushDeferredShaderDraws()/flushFboDeferredPbr() draw each sokol_gl
// layer with sgl_(context_)draw_layer(). The TrussC fork's _sgl_draw() used
// sg_append_buffer() and re-appended the ENTIRE vertex set on every call. With
// N layers (one per deferred PBR/shader draw) and V vertices recorded, one frame
// uploaded N*V vertices and grew the GPU buffer 2x at a time until the backend
// refused the allocation (Metal: id:52 / METAL_CREATE_BUFFER_FAILED), killing
// all sgl rendering. Root cause of the 2026-suzuki-rain fog vanishing.
//
// THE FIX: _sgl_draw() uploads the vertex set ONCE per frame and reuses the
// returned base offset for the remaining layer draws (O(V), not O(N*V)).
//
// WHY THIS IS HEADLESS / GPU-LESS: it drives sokol_gl on SOKOL_DUMMY_BACKEND,
// which has no GPU but tracks the exact accounting that blows up — append_pos
// (bytes uploaded this frame) and the GPU buffer size. So the regression is
// fully deterministic in CI. It is NOT a pixel/render-correctness test; that
// still needs real Metal/D3D11/GL devices (see the project note).
//
// Compiled as a standalone target (NOT a TrussC project) precisely because it
// must define its own SOKOL_IMPL with the dummy backend, which would clash with
// libTrussC's platform-backend sokol implementation.
//
// Console, exit code = pass/fail (build_all.py runs it under --core-tests-only).
// =============================================================================

#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include "sokol_log.h"
#include "sokol_gfx.h"
#include "util/sokol_gl_tc.h"

#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static const int STRIDE = (int)sizeof(_sgl_vertex_t);

// --- helpers ---------------------------------------------------------------

static void push_tris(int n_tris) {
    sgl_begin_triangles();
    for (int i = 0; i < n_tris; i++) {
        sgl_v2f(0.0f, 0.0f);
        sgl_v2f(1.0f, 0.0f);
        sgl_v2f(0.0f, 1.0f);
    }
    sgl_end();
}

static size_t append_pos(sgl_context ctx_id) {
    _sgl_context_t* ctx = _sgl_lookup_context(ctx_id.id);
    return (size_t)sg_query_buffer_info(ctx->vbuf).append_pos;
}
static size_t gpu_size(sgl_context ctx_id) {
    _sgl_context_t* ctx = _sgl_lookup_context(ctx_id.id);
    return sg_query_buffer_desc(ctx->vbuf).size;
}

// Flush layers [0..maxLayer], mimicking flushDeferredShaderDraws().
static void flush_layers(sgl_context ctx, int maxLayer) {
    for (int L = 0; L <= maxLayer; L++) sgl_context_draw_layer(ctx, L);
}

// =============================================================================

int main(void) {
    sg_setup(&(sg_desc){ .environment = (sg_environment){0}, .logger.func = slog_func });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    sgl_context ctx = sgl_make_context(&(sgl_context_desc_t){
        .max_vertices = 1<<16, .max_commands = 1<<14 });
    sgl_set_context(ctx);

    // -------------------------------------------------------------------------
    // 1) The core regression: empty layers must NOT each re-upload all vertices.
    //    Upload-per-frame must equal V (one upload), independent of layer count,
    //    and the GPU buffer must stay bounded.
    // -------------------------------------------------------------------------
    {
        const int fogTris = 1000;             // V = 3000 verts of "fog"
        const size_t V = (size_t)fogTris * 3 * STRIDE;
        bool oneUploadAllN = true;
        bool bufDoesNotGrowWithN = true;
        size_t bufAtSmallestN = 0;
        int Ns[] = { 1, 10, 100, 600 };
        for (int i = 0; i < 4; i++) {
            int N = Ns[i];
            sgl_layer(0);
            push_tris(fogTris);
            for (int L = 1; L <= N; L++) sgl_layer(L);   // N empty layers
            flush_layers(ctx, N);
            size_t pos = append_pos(ctx);
            size_t buf = gpu_size(ctx);
            printf("  [empty]   N=%4d  append_pos=%9zu (want %zu)  gpu_buf=%zu\n",
                   N, pos, V, buf);
            if (pos != V) oneUploadAllN = false;             // pre-fix: ~N*V
            // The GPU buffer must NOT grow as layer count increases. Pre-fix it
            // doubled every frame (2.5->5->10->...->GBs) until allocation failed.
            if (i == 0) bufAtSmallestN = buf;
            else if (buf > bufAtSmallestN) bufDoesNotGrowWithN = false;
            sg_commit();                                     // frame boundary
        }
        check("empty layers: exactly one upload per frame (no N*V)", oneUploadAllN);
        check("empty layers: GPU buffer does not grow with layer count", bufDoesNotGrowWithN);
    }

    // -------------------------------------------------------------------------
    // 2) Non-empty layers: the single upload must cover the FULL accumulated
    //    vertex set (fog + per-layer geometry), still exactly once.
    // -------------------------------------------------------------------------
    {
        const int fogTris = 500, perLayerTris = 2, N = 300;
        const size_t total = (size_t)(fogTris + N * perLayerTris) * 3 * STRIDE;
        sgl_layer(0);
        push_tris(fogTris);
        for (int L = 1; L <= N; L++) { sgl_layer(L); push_tris(perLayerTris); }
        flush_layers(ctx, N);
        size_t pos = append_pos(ctx);
        printf("  [nonempty] N=%d  append_pos=%zu (want %zu)\n", N, pos, total);
        check("non-empty layers: one upload of the full vertex set", pos == total);
        sg_commit();
    }

    // -------------------------------------------------------------------------
    // 3) Next frame must RE-upload (cache keyed on frame_id; not stale, not 0).
    // -------------------------------------------------------------------------
    {
        const int fogTris = 800;
        const size_t V = (size_t)fogTris * 3 * STRIDE;
        // frame A
        sgl_layer(0); push_tris(fogTris);
        for (int L = 1; L <= 50; L++) sgl_layer(L);
        flush_layers(ctx, 50);
        sg_commit();
        // frame B
        sgl_layer(0); push_tris(fogTris);
        for (int L = 1; L <= 50; L++) sgl_layer(L);
        flush_layers(ctx, 50);
        size_t pos = append_pos(ctx);
        printf("  [frame2]   append_pos=%zu (want %zu)\n", pos, V);
        check("new frame re-uploads its vertices (not stale)", pos == V);
        sg_commit();
    }

    // -------------------------------------------------------------------------
    // 4) sgl_tc_context_reset() within a frame must invalidate the upload cache:
    //    a fresh vertex set whose count coincides with the previous one must NOT
    //    reuse the stale base offset (it must append again at a new offset).
    // -------------------------------------------------------------------------
    {
        const int tris = 600;
        const size_t C = (size_t)tris * 3 * STRIDE;
        sgl_layer(0); push_tris(tris);
        flush_layers(ctx, 0);
        size_t pos1 = append_pos(ctx);                 // == C
        sgl_tc_context_reset(ctx);                     // reset counters, same frame
        sgl_layer(0); push_tris(tris);                 // SAME count as before
        flush_layers(ctx, 0);
        size_t pos2 = append_pos(ctx);                 // must have appended again
        printf("  [reset]    pos1=%zu  pos2=%zu (want %zu)\n", pos1, pos2, 2 * C);
        check("context reset invalidates cache (re-appends, no stale reuse)",
              pos1 == C && pos2 == 2 * C);
        sg_commit();
    }

    sgl_destroy_context(ctx);
    sgl_shutdown();
    sg_shutdown();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
