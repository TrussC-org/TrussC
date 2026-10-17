// Regression from #821 PR 3 Review round 1. No window, real Shader/Fbo APIs.
#if defined(__linux__) && !defined(__ANDROID__)
#undef SOKOL_GLCORE
#undef SOKOL_GLES3
#define SOKOL_DUMMY_BACKEND
#define SOKOL_TRACE_HOOKS
#define SOKOL_GFX_IMPL
#include "sokol/sokol_gfx.h"
#undef SOKOL_GFX_IMPL
#define SOKOL_GL_IMPL
#include "sokol/util/sokol_gl_tc.h"
#undef SOKOL_GL_IMPL
#include <TrussC.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace tc;
// Generated native shaders have no dummy-backend descriptor. The dummy sgl
// shader is sufficient for descriptor/lifetime probes; GPU suites test pixels.
namespace trussc::internal {
sg_shader sglPremultShader() { return {}; }
sg_shader sglCoverageShader() { return {}; }
}
// One-shot failure at the real deferred replay allocation, as in the review.
static bool failAllocation = false;
void* operator new(std::size_t size) {
    if (failAllocation) { failAllocation = false; throw std::bad_alloc(); }
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); }
    catch (...) { return nullptr; }
}
void operator delete(void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
namespace {
struct LateShader : Shader {
    sg_pipeline selection() { return pipelineForCurrentTarget(); }
};
// Constructed before the host registries, destroyed after them and sg_shutdown.
LateShader lateShader;
int submittedDraws = 0;
void drawn(int, int, int, void*) { ++submittedDraws; }
std::vector<sg_pass> passes;
int commits = 0;
sg_pipeline_desc appliedPipeline = {};
void applied(sg_pipeline pipeline, void*) { appliedPipeline = sg_query_pipeline_desc(pipeline); }
void began(const sg_pass* pass, void*) { passes.push_back(*pass); }
void committed(void*) { ++commits; }
const sg_shader_desc* descriptor(sg_backend) { static sg_shader_desc desc = {}; return &desc; }
void submit(Shader& shader) {
    ShaderVertex vertices[3] = {};
    shader.submitVertices(vertices, 3, PrimitiveType::Triangles);
}
sg_swapchain dummySwapchain(void*) {
    sg_swapchain result = {};
    result.width = 256; result.height = 192;
    result.color_format = SG_PIXELFORMAT_RGBA8;
    result.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    result.sample_count = 1;
    return result;
}
bool probe(bool parentFlush) {
    submittedDraws = 0;
    internal::WindowContext window;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    window.swapchainTarget.resources().context = sgl_default_context();
    // Both modes use exactly the same dummy pass in place of a native drawable.
    window.acquireSwapchain = dummySwapchain;
    Shader shader;
    if (!shader.load(descriptor)) return false;
    Fbo fbo;
    fbo.allocate(16, 16);
    internal::ensureCurrentPass();
    shader.begin(); submit(shader);
    if (parentFlush) internal::currentTarget().flushLayers();
    fbo.begin(0, 0, 0, 1); fbo.end();
    submit(shader);
    const auto& target = internal::currentTarget();
    const bool layers = target.sglLayerNext > 0 && target.deferredShaderDraws.back().layerId >= 0;
    std::printf("POST_RESUME layerNext=%d layerId=%d\n", target.sglLayerNext,
                target.deferredShaderDraws.back().layerId);
    shader.end(); internal::currentTarget().flushLayers();
    const bool reset = internal::currentTarget().sglLayerNext == 0;
    std::printf("RESULT parentFlush=%d draws=%d expected=2\n", parentFlush, submittedDraws);
    window.swapchainTarget.endPass();
    internal::currentWindowCtx() = previous;
    sg_commit();
    sgl_tc_context_reset(sgl_default_context());
    return layers && reset && submittedDraws == 2;
}
bool leaseFailureProbe() {
    internal::WindowContext window;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    window.swapchainTarget.resources().context = sgl_default_context();
    window.acquireSwapchain = dummySwapchain;
    internal::ensureCurrentPass();
    Fbo parent, child;
    parent.allocate(16,16);
    child.allocate(8,8,1,TextureFormat::RGBA16F);
    parent.begin(0,0,0);
    auto* selected = &internal::currentTarget();
    const auto token = selected->activation;
    const auto context = sgl_get_context();
    // Force the vendor's documented allocation-failure return, without a
    // production-only test hook or exhausting the machine's memory.
    const auto allocator = _sgl.desc.allocator;
    const int slots = _sgl.context_pool.pool.queue_top;
    _sgl.context_pool.pool.queue_top = 0;
    _sgl.desc.allocator.alloc_fn = [](size_t,void*) -> void* { return nullptr; };
    child.begin(0,0,0);
    _sgl.desc.allocator = allocator;
    _sgl.context_pool.pool.queue_top = slots;
    const bool intact = !child.isActive() && &internal::currentTarget() == selected &&
        selected->activation == token && selected->inPass && sgl_get_context().id == context.id &&
        window.targetStack.size() == 2;
    std::printf("LEASE FAILURE parent intact=%d expected=1\n",intact);
    parent.end(); window.swapchainTarget.endPass();
    internal::currentWindowCtx() = previous;
    sg_commit(); sgl_tc_context_reset(sgl_default_context());
    return intact;
}

bool storePolicyProbe() {
    internal::WindowContext window;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    window.swapchainTarget.resources().context = sgl_default_context();
    window.acquireSwapchain = dummySwapchain;
    passes.clear(); const int before = commits;
    present();
    const bool finalTail = passes.size() == 1 && passes[0].action.depth.store_action == SG_STOREACTION_DONTCARE && commits == before+1;
    Fbo child; child.allocate(8,8,4);
    auto callback = events().onRender.listen([&] { child.begin(0,0,0); child.end(); });
    passes.clear();
    present();
    const bool resumable = passes.size() == 3 && passes[0].action.depth.store_action == SG_STOREACTION_STORE &&
        passes[1].action.colors[0].store_action == SG_STOREACTION_STORE &&
        passes[2].action.colors[0].load_action == SG_LOADACTION_LOAD &&
        passes[2].action.depth.load_action == SG_LOADACTION_LOAD && commits == before+2;
    std::printf("STORE POLICY finalTail=%d callbackResume=%d commits=%d expected=2\n", finalTail,resumable,commits-before);
    internal::currentWindowCtx() = previous;
    return finalTail && resumable;
}

bool inheritedPipelineProbe() {
    internal::WindowContext window;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    window.swapchainTarget.resources().context = sgl_default_context();
    window.acquireSwapchain = dummySwapchain;
    Fbo fbo; fbo.allocate(16,16);
    bool ok = true;
    for (bool depth : {false,true}) {
        setBlendMode(BlendMode::Add);
        if (depth) enableDepthTest(); else disableDepthTest();
        fbo.begin(0,0,0);
        const auto role = internal::currentTarget().selectedPipeline;
        setColor(1,0,0,0.5f); drawRect(0,0,16,16);
        setColor(0,1,0,0.5f); drawRect(0,0,16,16);
        fbo.end();
        const bool correct = role == internal::fillSelection(BlendMode::Add, depth) &&
            appliedPipeline.depth.write_enabled == depth &&
            appliedPipeline.depth.compare == (depth ? SG_COMPAREFUNC_LESS_EQUAL : SG_COMPAREFUNC_ALWAYS) &&
            appliedPipeline.colors[0].blend.src_factor_rgb == SG_BLENDFACTOR_SRC_ALPHA &&
            appliedPipeline.colors[0].blend.dst_factor_rgb == SG_BLENDFACTOR_ONE;
        std::printf("INHERITED PIPELINE depth=%d role=0x%x descriptor=%d expected=1\n",depth,unsigned(role.role),correct);
        ok &= correct;
    }
    window.teardownTargets();
    internal::currentWindowCtx() = previous;
    sg_commit();
    return ok;
}

bool replayFailureProbe(bool destructor) {
    internal::WindowContext window;
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    window.swapchainTarget.resources().context = sgl_default_context();
    window.acquireSwapchain = dummySwapchain;
    Fbo fbo; fbo.allocate(16,16);
    internal::ensureCurrentPass();
    internal::RenderTarget target;
    target.passKind = internal::TargetPassKind::Fixed;
    target.isFbo = true; target.width = target.height = 16;
    target.sampleCount = 1; target.colorFormat = SG_PIXELFORMAT_RGBA8;
    target.binding.attachments.colors[0] = fbo.getTexture().getAttachmentView();
    target.hasDepth = false; target.depthFormat = SG_PIXELFORMAT_NONE;
    auto* lease = internal::leaseTargetResources(SG_PIXELFORMAT_RGBA8,1,SG_PIXELFORMAT_NONE);
    target.pooledResources = lease;
    Shader shader; shader.load(descriptor);
    bool caught = false;
    {
        internal::TargetPassScope scope(target);
        shader.begin(); submit(shader); shader.end();
        failAllocation = true;
        if (!destructor) {
            try { internal::popTarget(target,target.activation); }
            catch (const std::bad_alloc&) { caught = true; }
        }
    }
    const bool ok = !failAllocation && (destructor || caught) && window.targetStack.size() == 1 &&
        window.currentTarget == &window.swapchainTarget && !target.activation && !target.activeWindow &&
        !target.inPass && window.swapchainTarget.inPass && !lease->leased && !target.pooledResources &&
        target.deferredShaderDraws.empty() && sgl_get_context().id == sgl_default_context().id;
    std::printf("REPLAY BAD_ALLOC destructor=%d restored=%d expected=1\n",destructor,ok);
    // A following activation must reuse the released lease, with no stale draw.
    const int before = submittedDraws;
    fbo.begin(0,0,0); fbo.end();
    const bool reusable = !lease->leased && submittedDraws == before;
    window.teardownTargets();
    internal::currentWindowCtx() = previous;
    sg_commit();
    return ok && reusable;
}

bool teardownProbe(bool explicitTeardown, bool otherCurrent) {
    auto* previous = internal::currentWindowCtx();
    internal::WindowContext other;
    auto* window = new internal::WindowContext;
    internal::currentWindowCtx() = window;
    window->swapchainTarget.resources().context = sgl_default_context();
    window->acquireSwapchain = dummySwapchain;
    Fbo fbo; fbo.allocate(16,16);
    internal::ensureCurrentPass();
    Shader shader; shader.load(descriptor); shader.begin();
    auto& target = window->shadow.target;
    target.passKind = internal::TargetPassKind::Fixed;
    target.isFbo = true; target.width = target.height = 16;
    target.sampleCount = 1; target.colorFormat = SG_PIXELFORMAT_RGBA8;
    target.binding.attachments.colors[0] = fbo.getTexture().getAttachmentView();
    target.hasDepth = false; target.depthFormat = SG_PIXELFORMAT_NONE;
    auto* lease = internal::leaseTargetResources(SG_PIXELFORMAT_RGBA8,1,SG_PIXELFORMAT_NONE);
    target.pooledResources = lease;
    window->shadow.scope = std::make_unique<internal::TargetPassScope>(target);
    window->shadow.inPass = true;
    // Include unmatched child bookkeeping as well as the parked screen scope.
    shader.begin();
    if (otherCurrent) internal::currentWindowCtx() = &other;
    bool clean = true;
    if (explicitTeardown) {
        window->teardownTargets();
        window->teardownTargets();
        clean = !window->shadow.scope && !window->shadow.inPass && !target.activation &&
            window->targetStack.size() == 1 && window->shaderScopeTargets.empty() &&
            window->shaderStack.empty() && !window->swapchainTarget.inPass;
    }
    delete window; // The original review regression: members must still be alive.
    const bool restored = internal::currentWindowCtx() == (otherCurrent ? &other : nullptr);
    clean &= restored && !lease->leased;
    std::printf("WINDOW TEARDOWN explicit=%d otherCurrent=%d restored=%d expected=1\n",
                explicitTeardown,otherCurrent,clean);
    internal::currentWindowCtx() = previous;
    sg_commit();
    return clean;
}

bool pipelineGenerationProbe() {
    auto& window = internal::mainWindowContext();
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    auto& target = window.swapchainTarget;
    target.resources().context = sgl_default_context();
    target.width=256; target.height=192; target.colorFormat=SG_PIXELFORMAT_RGBA8; target.sampleCount=1;
    window.acquireSwapchain=dummySwapchain;
    sg_shader shader = sg_make_shader(descriptor(SG_BACKEND_DUMMY));
    sg_pipeline_desc desc = {}; desc.shader=shader;
    desc.layout.attrs[0].format=SG_VERTEXFORMAT_FLOAT3;
    auto request = internal::PipelineKey::raw(internal::PipelineRole::Shader,desc);
    const auto first=target.pipeline(request);
    const bool lateSelected=lateShader.selection().id!=0;
    bool ok=lateSelected &&first.raw.id && first.raw.id==target.pipeline(request).raw.id;
    // Exhaust the real dummy pool, rather than fabricating an invalid recipe.
    std::vector<sg_pipeline> held;
    for (;;) { auto pipeline=sg_alloc_pipeline(); if(!pipeline.id) break; held.push_back(pipeline); }
    desc.colors[0].blend.enabled=true;
    auto failedRequest=internal::PipelineKey::raw(internal::PipelineRole::Shader,desc);
    const auto failed=target.pipeline(failedRequest);
    const auto failedImmediate=internal::active3D();
    internal::loadPipeline(failedImmediate);
    ok &= !failedImmediate.id && target.selectedPipeline == internal::ImmediateSelection{internal::PipelineRole::Geometry3D,BlendMode::Alpha,true};
    const auto entries=target.resources().rawCache->cache.size();
    for(auto pipeline:held) sg_destroy_pipeline(pipeline);
    ok &= !failed.raw.id && !target.pipeline(failedRequest).raw.id && target.resources().rawCache->cache.size()==entries && !internal::active3D().id;
    Fbo survivor; survivor.allocate(16,12);
    for(int iteration=0;iteration<50;++iteration) {
        survivor.begin(0,0,0);
        const auto old=internal::currentTarget().resources().generation;
        survivor.end();
        if(iteration==0) {
            auto* ctx=_sgl_lookup_context(target.resources().context.id);
            std::printf("RESIZE BOUNDARY commands=%d base=%d vertices=%d base=%d queued=%zu/%zu/%zu pass=%d scopes=%u\n",ctx->commands.next,ctx->draw_base_cmd,ctx->vertices.next,ctx->draw_base_vertex,target.deferredShaderDraws.size(),target.deferredPbrDraws.size(),target.deferredPointDraws.size(),target.inPass,target.openShaderScopes);
        }
        auto& budget=internal::sglBudget(); internal::resizeSgl(budget.maxVertices,budget.maxCommands);
        survivor.begin(0,0,0);
        ok &= internal::currentTarget().resources().generation!=old;
        survivor.end();
        const auto retried=target.pipeline(failedRequest);
        ok &= retried.raw.id!=0;
        sg_commit(); internal::drainPendingGpuDestroys();
    }
    internal::retirePipelineProgram(shader); internal::deferGpuDestroy(shader);
    sg_commit(); internal::drainPendingGpuDestroys();
    internal::currentWindowCtx()=previous;
    std::printf("PIPELINE failure cached/retry + resize x50=%d expected=1\n",ok);
    return ok;
}

// Adversarial review: recording on a lazy bottom target must survive both
// rebuild paths, and memory release must follow the rebuilt window context.
bool lazyResizeProbe(bool dimensions) {
    auto& window = internal::mainWindowContext();
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window;
    auto& target = window.swapchainTarget;
    const bool wasMain = window.isMain;
    window.isMain = false; window.fbWidth = 256; window.fbHeight = 192;
    window.acquireSwapchain = dummySwapchain;
    internal::prepareWindowTarget();
    const auto before = target.resources().context.id;
    const auto generationBefore = target.resources().generation;
    const int oldVertices = internal::sglBudget().maxVertices;
    const int oldCommands = internal::sglBudget().maxCommands;
    internal::loadPipeline(internal::activeFill2D());
    sgl_begin_triangles(); sgl_v2f(0,0); sgl_v2f(1,0); sgl_v2f(0,1); sgl_end();
    const bool lazy = !target.inPass && sgl_num_commands() == 1;
    if (dimensions) {
        window.fbWidth = 320; window.fbHeight = 240;
        internal::prepareWindowTarget();
    } else internal::resizeSgl(1024,256);
    const bool deferred = target.resources().context.id == before && sgl_num_commands() == 1;
    submittedDraws = 0;
    present();
    bool ok = lazy && deferred && submittedDraws == 1;
    std::printf("LAZY RESIZE dimensions=%d deferred=%d draws=%d expected=1\n",dimensions,deferred,submittedDraws);
    // beginFrame retries the dimension change or the exact deferred budget.
    beginFrame();
    auto* actual = _sgl_lookup_context(target.resources().context.id);
    ok &= target.resources().generation != generationBefore && target.resources().context.id != sgl_default_context().id;
    if (!dimensions) ok &= actual->desc.max_vertices == 1024 && actual->desc.max_commands == 256 && !internal::sglBudget().pendingResize;
    // Force real CPU and GPU growth beyond the rebuilt capacities.
    const int initialVertices = actual->vertices.cap;
    internal::loadPipeline(internal::activeFill2D());
    sgl_begin_triangles();
    for (int i=0; i<initialVertices+6; ++i) sgl_v2f(0,0);
    sgl_end();
    present();
    const int grownVertices = actual->vertices.cap;
    const bool grew = grownVertices > initialVertices && actual->vbuf.id &&
        sg_query_buffer_desc(actual->vbuf).size > size_t(initialVertices)*sizeof(_sgl_vertex_t);
    releaseSglBuffers();
    const bool released = actual->vertices.cap == 0 && actual->commands.cap == 0 &&
        actual->uniforms.cap == 0 && actual->vbuf.id == 0;
    std::printf("REBUILT RELEASE dimensions=%d grew=%d vertices=%d->%d capacities=%d/%d/%d vbuf=%u expected=0\n",
        dimensions,grew,initialVertices,grownVertices,actual->vertices.cap,actual->commands.cap,actual->uniforms.cap,actual->vbuf.id);
    ok &= grew && released;
    internal::resizeSgl(oldVertices,oldCommands);
    window.isMain = wasMain;
    internal::currentWindowCtx() = previous;
    return ok;
}

// Register a secondary coordinator with a dummy drawable so the production
// global rebuild guard sees both windows, without creating a native window.
struct DummyWindowPair {
    Window secondary;
    internal::WindowContext& main = internal::mainWindowContext();
    internal::WindowContext* previous = internal::currentWindowCtx();
    bool wasMain = main.isMain;
    DummyWindowPair() {
        secondary.native_ = this; // isOpen() only; never passed to native APIs.
        main.isMain = false;
        for (auto* window : {&main, &secondary.context()}) {
            window->fbWidth = 256; window->fbHeight = 192;
            window->acquireSwapchain = dummySwapchain;
            internal::currentWindowCtx() = window;
            internal::prepareWindowTarget();
        }
        internal::restoreWindowContext(previous);
    }
    ~DummyWindowPair() {
        secondary.native_ = nullptr;
        secondary.context().teardownTargets();
        secondary.context().swapchainTarget.resources().release();
        main.isMain = wasMain;
        internal::restoreWindowContext(previous);
    }
};

void recordTriangle() {
    internal::loadPipeline(internal::activeFill2D());
    sgl_begin_triangles(); sgl_v2f(0,0); sgl_v2f(1,0); sgl_v2f(0,1); sgl_end();
}

bool windowTickRestoreProbe(bool dimensions) {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    if (dimensions) {
        windows.main.fbWidth = 320; windows.main.fbHeight = 240;
        internal::prepareWindowTarget();
    } else internal::resizeSgl(1024,256);
    const auto mainContext = internal::currentTarget().resources().context;
    bool ok = mainContext.id && mainContext.id != sgl_default_context().id;
    // Mirror a secondary tick, including the exact helper used by all native
    // adapters. The main recording happens AFTER that tick's global commit.
    auto* previous = internal::currentWindowCtx();
    internal::restoreWindowContext(&windows.secondary.context());
    sgl_defaults(); beginFrame(); present();
    internal::restoreWindowContext(previous);
    const bool restored = sgl_get_context().id == mainContext.id;
    recordTriangle();
    beginFrame();
    submittedDraws = 0;
    present();
    ok &= restored && submittedDraws == 1;
    std::printf("WINDOW TICK RESTORE dimensions=%d actual=%u selected=%u restored=%d draws=%d expected=1\n",
        dimensions,mainContext.id,sgl_get_context().id,restored,submittedDraws);
    // A boundary may also return to a parked Fbo, rather than a bottom target.
    Fbo fbo; fbo.allocate(8,8); fbo.begin(0,0,0);
    const auto fboContext = internal::currentTarget().resources().context;
    internal::restoreWindowContext(&windows.secondary.context());
    internal::restoreWindowContext(previous);
    ok &= sgl_get_context().id == fboContext.id;
    fbo.end(); present();
    return ok;
}

// Document the pre-existing global rewind honestly: B's present loses A's
// lazy immediate triangle even WITHOUT resize. Raw deferred work survives,
// so it must continue to block a rebuild after that global SGL rewind.
bool interleavedRewindProbe(bool resize, bool raw) {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    auto& target = windows.main.swapchainTarget;
    const auto before = target.resources().context;
    const auto generation = target.resources().generation;
    recordTriangle();
    Shader shader;
    bool ok = !target.inPass && sgl_tc_context_has_pending_draws(before);
    if (raw) {
        ok &= shader.load(descriptor);
        shader.begin(); submit(shader); shader.end();
        ok &= !target.deferredShaderDraws.empty();
    }
    internal::restoreWindowContext(&windows.secondary.context());
    if (resize) {
        internal::resizeSgl(2048,333);
        // A repeated request must retain the latest explicit capacities.
        internal::resizeSgl(3072,377);
        ok &= internal::sglBudget().pendingResize == 3072 &&
            internal::sglBudget().pendingCommands == 377 && target.resources().context.id == before.id;
        beginFrame(); // A still has pending work: this retry must defer too.
        ok &= target.resources().context.id == before.id && sgl_tc_context_has_pending_draws(before);
    }
    present(); // Global commit rewinds A as well as B.
    const bool rewound = !sgl_tc_context_has_pending_draws(before);
    ok &= rewound;
    internal::restoreWindowContext(nullptr);
    if (resize) {
        beginFrame(); // Rebuild now, unless A still owns a raw deferred draw.
        if (raw) ok &= internal::sglBudget().pendingResize == 3072 && target.resources().context.id == before.id;
        else ok &= !internal::sglBudget().pendingResize && target.resources().generation != generation;
    }
    submittedDraws = 0;
    present();
    const int draws = submittedDraws;
    ok &= draws == (raw ? 1 : 0); // The immediate triangle was already lost.
    if (resize) {
        beginFrame(); // Raw work has now drained too: first safe retry succeeds.
        auto* context = _sgl_lookup_context(target.resources().context.id);
        ok &= !internal::sglBudget().pendingResize && context->desc.max_vertices == 3072 && context->desc.max_commands == 377;
    }
    std::printf("CROSS WINDOW REWIND resize=%d raw=%d rewound=%d draws=%d expected=%d retry=%d\n",
        resize,raw,rewound,draws,raw ? 1 : 0,ok);
    return ok;
}

bool activeDestructionProbe() {
    std::fflush(stdout);
    const pid_t child = fork();
    if (child == 0) {
        rlimit limit = {0,0}; setrlimit(RLIMIT_CORE,&limit);
        internal::WindowContext window;
        internal::currentWindowCtx() = &window;
        window.swapchainTarget.resources().context = sgl_default_context();
        window.acquireSwapchain = dummySwapchain;
        { Fbo active; active.allocate(8,8); active.begin(0,0,0); }
        _exit(3);
    }
    if (child < 0) return false;
    int status = 0;
    const bool rejected = waitpid(child,&status,0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    std::printf("ACTIVE DESTRUCTION rejected=%d expected=1\n",rejected);
    return rejected;
}

}
int main(int argc, char** argv) {
    sg_desc desc = {};
    // Multiple independent windows warm caches in this one process.
    desc.pipeline_pool_size = 2048;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    desc.environment.defaults.sample_count = 1;
    sg_setup(&desc);
    sgl_desc_t gl = {}; gl.pipeline_pool_size = 256; sgl_setup(&gl);
    if (!lateShader.load(descriptor)) return 1;
    sg_trace_hooks hooks = {}; hooks.draw = drawn; hooks.begin_pass = began; hooks.commit = committed; hooks.apply_pipeline = applied; sg_install_trace_hooks(&hooks);
    bool teardown = true;
    for (bool explicitTeardown : {false,true}) for (bool otherCurrent : {false,true})
        teardown &= teardownProbe(explicitTeardown,otherCurrent);
    if (argc > 1 && std::strcmp(argv[1],"--teardown") == 0) return teardown ? 0 : 1;
    const bool inherited = inheritedPipelineProbe();
    const bool replay = replayFailureProbe(false) && replayFailureProbe(true);
    const bool normal = probe(false);
    const bool flushed = probe(true);
    const bool failedLease = leaseFailureProbe();
    const bool stores = storePolicyProbe();
    const bool pipelines = pipelineGenerationProbe();
    const bool lazyResize = lazyResizeProbe(false) && lazyResizeProbe(true);
    const bool tickRestore = windowTickRestoreProbe(false) && windowTickRestoreProbe(true);
    const bool interleaved = interleavedRewindProbe(false,false) && interleavedRewindProbe(true,false) && interleavedRewindProbe(true,true);
    const bool destruction = activeDestructionProbe();
    const int result=teardown && inherited && replay && normal && flushed && failedLease && stores && pipelines && lazyResize && tickRestore && interleaved && destruction ? 0 : 1;
    sgl_shutdown(); sg_shutdown();
    return result;
}
#else
#include <cstdio>
int main() {
    std::puts("SKIP: ELF dummy-backend override probe runs on Linux; use renderPassStack --gpu-check on this backend");
    return 0;
}
#endif
