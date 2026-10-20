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
// ELF overrides of host-private native checks; no production hook or Sokol
// seam. Simulate a null native object while the dummy SG handle stays VALID.
static int nativeChecksUntilFailure = -1;
static bool nativeFailureWasValid = false;
template<class Handle, class Query>
bool checkedNativeObject(Handle handle, Query query) {
    if (nativeChecksUntilFailure < 0) return true;
    if (nativeChecksUntilFailure-- != 0) return true;
    nativeFailureWasValid = query(handle) == SG_RESOURCESTATE_VALID;
    return false;
}
// Generated native shaders have no dummy-backend descriptor. The dummy sgl
// shader is sufficient for descriptor/lifetime probes; GPU suites test pixels.
namespace trussc::internal {
sg_shader sglPremultShader() { return {}; }
sg_shader sglCoverageShader() { return {}; }
bool shadowNativeValid(sg_image handle) { return checkedNativeObject(handle, sg_query_image_state); }
bool shadowNativeValid(sg_view handle) { return checkedNativeObject(handle, sg_query_view_state); }
bool shadowNativeValid(sg_sampler handle) { return checkedNativeObject(handle, sg_query_sampler_state); }
}
// One-shot failure at the real deferred replay allocation, as in the review.
static bool failAllocation = false;
// Unlike the older one-shot probes, reject every allocation until explicitly
// restored, including allocations attempted during cleanup and error delivery.
static int allocationsUntilFailure = -1;
static int rejectedAllocations = 0;
void* operator new(std::size_t size) {
    if (allocationsUntilFailure == 0) { ++rejectedAllocations; throw std::bad_alloc(); }
    if (allocationsUntilFailure > 0) --allocationsUntilFailure;
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
        internal::endGpuFrame();
    }
    internal::retirePipelineProgram(shader); internal::deferGpuDestroy(shader);
    internal::endGpuFrame();
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

// Foreign commits preserve every unconsumed recording and defer exact resize requests.
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
    present(); // B completes only B.
    const bool rewound = !sgl_tc_context_has_pending_draws(before);
    ok &= !rewound;
    internal::restoreWindowContext(nullptr);
    if (resize) {
        beginFrame(); // Rebuild now, unless A still owns a raw deferred draw.
        ok &= internal::sglBudget().pendingResize == 3072 && target.resources().context.id == before.id && target.resources().generation == generation;
    }
    submittedDraws = 0;
    present();
    const int draws = submittedDraws;
    ok &= draws == (raw ? 2 : 1);
    if (resize) {
        beginFrame(); // Raw work has now drained too: first safe retry succeeds.
        auto* context = _sgl_lookup_context(target.resources().context.id);
        ok &= !internal::sglBudget().pendingResize && context->desc.max_vertices == 3072 && context->desc.max_commands == 377;
    }
    std::printf("CROSS WINDOW REWIND resize=%d raw=%d rewound=%d draws=%d expected=%d retry=%d\n",
        resize,raw,rewound,draws,raw ? 2 : 1,ok);
    return ok;
}

// Exercise the production no-draw predicate and endGpuFrame, with an actual
// unrelated buffer update. No replacement scheduler/reset helper is involved.
bool noDrawParityProbe(bool upload, bool raw) {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    present();
    Shader shader;
    bool ok = shader.load(descriptor);
    recordTriangle();
    if (raw) { shader.begin(); submit(shader); shader.end(); }
    auto& target = windows.main.swapchainTarget;
    sg_buffer buffer = {};
    if (upload) {
        sg_buffer_desc desc = {}; desc.size = 16; desc.usage.vertex_buffer = true; desc.usage.dynamic_update = true;
        buffer = sg_make_buffer(desc);
        uint32_t bytes[4] = {}; sg_update_buffer(buffer, SG_RANGE(bytes));
    }
    const bool work = tc_internal_gpu_frame_has_work();
    ok &= work == upload;
    if (work) internal::endGpuFrame(); // TrussC's conditional no-draw boundary
    ok &= !target.inPass && target.deferredShaderDraws.size() == (raw ? 1u : 0u);
    ok &= sgl_tc_context_has_pending_draws(target.resources().context) == !upload;
    submittedDraws = 0;
    present();
    const int expected = (raw ? 1 : 0) + (upload ? 0 : 1);
    ok &= submittedDraws == expected && target.deferredShaderDraws.empty();
    std::printf("NO DRAW PARITY upload=%d raw=%d gpu_work=%d draws=%d expected=%d ok=%d\n",
        upload, raw, work, submittedDraws, expected, ok);
    if (buffer.id) sg_destroy_buffer(buffer);
    return ok;
}

// The generated native PBR/point shaders have no dummy descriptors. Supply
// valid dummy pipelines to their real captured-command replay paths instead.
bool rawKindsLifetimeProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    present();
    auto& target = windows.main.swapchainTarget;
    sg_shader_desc pbrDesc = {}, pointDesc = {};
    pbrDesc.uniform_blocks[UB_tc_pbr_vs_params].stage = SG_SHADERSTAGE_VERTEX;
    pbrDesc.uniform_blocks[UB_tc_pbr_vs_params].size = sizeof(tc_pbr_vs_params_t);
    pbrDesc.uniform_blocks[UB_tc_pbr_fs_params].stage = SG_SHADERSTAGE_FRAGMENT;
    pbrDesc.uniform_blocks[UB_tc_pbr_fs_params].size = sizeof(tc_pbr_fs_params_t);
    pointDesc.uniform_blocks[UB_tc_pointcloud_vs_params].stage = SG_SHADERSTAGE_VERTEX;
    pointDesc.uniform_blocks[UB_tc_pointcloud_vs_params].size = sizeof(tc_pointcloud_vs_params_t);
    const sg_shader pbrShader = sg_make_shader(&pbrDesc), pointShader = sg_make_shader(&pointDesc);
    sg_pipeline_desc pd = {}; pd.shader = pbrShader;
    pd.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    const sg_pipeline pbrPipeline = sg_make_pipeline(&pd);
    pd.shader = pointShader;
    const sg_pipeline pointPipeline = sg_make_pipeline(&pd);
    auto generation = std::make_shared<internal::PipelineGeneration>();
    std::weak_ptr<internal::PipelineGeneration> lifetime = generation;
    const float vertices[9] = {};
    sg_buffer_desc bd = {}; bd.data = SG_RANGE(vertices);
    const sg_buffer buffer = sg_make_buffer(&bd);
    internal::PbrDrawCommand pbr = {}; pbr.pip = pbrPipeline; pbr.generation = generation;
    pbr.bind.vertex_buffers[0] = buffer;
    pbr.vertexCount = 3;
    std::memset(&pbr.vsp, 0x21, sizeof(pbr.vsp));
    std::memset(&pbr.fsp, 0x32, sizeof(pbr.fsp));
    internal::PointDrawCommand point = {}; point.pip = pointPipeline; point.generation = generation;
    point.bind.vertex_buffers[0] = buffer;
    point.numElements = 3; point.numInstances = 1;
    std::memset(&point.vsp, 0x43, sizeof(point.vsp));
    bool ok = internal::appendTargetRecording(target, target.deferredPbrDraws,
        internal::DeferredPbrDraw{target.sglLayerNext, pbr}) && target.advanceLayer();
    ok &= internal::appendTargetRecording(target, target.deferredPointDraws,
        internal::DeferredPointDraw{target.sglLayerNext, point}) && target.advanceLayer();
    pbr.generation.reset(); point.generation.reset(); generation.reset();
    internal::deferGpuDestroy(buffer);
    internal::endGpuFrame(); // an own upload/completion does not consume raw draws
    internal::restoreWindowContext(&windows.secondary.context());
    for (int i = 0; i < 3; ++i) present();
    internal::restoreWindowContext(nullptr);
    ok &= !lifetime.expired() && target.deferredPbrDraws.size() == 1 && target.deferredPointDraws.size() == 1 &&
        sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID;
    if (ok) {
        const auto& savedPbr = target.deferredPbrDraws.front();
        const auto& savedPoint = target.deferredPointDraws.front();
        ok &= savedPbr.layerId < savedPoint.layerId &&
            !std::memcmp(&savedPbr.cmd.vsp, &pbr.vsp, sizeof(pbr.vsp)) &&
            !std::memcmp(&savedPbr.cmd.fsp, &pbr.fsp, sizeof(pbr.fsp)) &&
            !std::memcmp(&savedPoint.cmd.vsp, &point.vsp, sizeof(point.vsp));
    }
    submittedDraws = 0; present();
    ok &= submittedDraws == 2 && target.deferredPbrDraws.empty() && target.deferredPointDraws.empty() && lifetime.expired() &&
        sg_query_buffer_state(buffer) == SG_RESOURCESTATE_INVALID;
    present(); ok &= submittedDraws == 2;
    std::printf("RAW KINDS PBR_and_point copied_uniforms_pins_consume_once=%d draws=%d expected=2\n", ok, submittedDraws);
    sg_destroy_pipeline(pbrPipeline); sg_destroy_pipeline(pointPipeline);
    sg_destroy_shader(pbrShader); sg_destroy_shader(pointShader);
    return ok;
}

bool parkedWindowProbe(bool fboTop) {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    Fbo fbo;
    if (fboTop) { fbo.allocate(16,16,4); fbo.begin(0,0,0); }
    internal::ensureCurrentPass();
    auto& target = internal::currentTarget();
    const auto token = target.activation;
    const auto context = target.resources().context;
    const auto binding = target.binding;
    Shader shader;
    bool ok = shader.load(descriptor);
    shader.begin(); submit(shader); recordTriangle();
    const auto stackSize = windows.main.targetStack.size();
    const auto startDraws = submittedDraws;
    internal::restoreWindowContext(&windows.secondary.context());
    ok &= !target.inPass && target.activation == token && target.openShaderScopes == 1 &&
        windows.main.targetStack.size() == stackSize && submittedDraws == startDraws + 2;
    for (int i = 0; i < 3; ++i) present();
    internal::restoreWindowContext(nullptr);
    ok &= !target.inPass && sgl_get_context().id == context.id;
    const auto beforePrefix = commits;
    internal::submitCapturePrefix();
    ok &= commits == beforePrefix && target.openShaderScopes == 1;
    internal::ensureCurrentPass();
    ok &= target.inPass && target.activation == token &&
        passes.back().action.colors[0].load_action == SG_LOADACTION_LOAD &&
        target.binding.attachments.colors[0].id == binding.attachments.colors[0].id;
    submit(shader); shader.end();
    if (fboTop) fbo.end();
    present();
    ok &= submittedDraws == startDraws + 3;
    std::printf("PARK WINDOW fbo=%d LOAD=1 prefix_commits=0 draws=%d expected=3 ok=%d\n",
        fboTop, submittedDraws - startDraws, ok);
    return ok;
}

// Raw PBR precedes newly recorded SGL state, so native state must already
// match the parked target when shadow-slot replay begins.
bool shadowReplayDrawingStateProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    auto& target = internal::currentTarget();
    bool ok = internal::ensureShadowStorage(windows.main,64);
    sg_shader_desc sd = {};
    sd.uniform_blocks[UB_tc_pbr_vs_params].stage = SG_SHADERSTAGE_VERTEX;
    sd.uniform_blocks[UB_tc_pbr_vs_params].size = sizeof(tc_pbr_vs_params_t);
    sd.uniform_blocks[UB_tc_pbr_fs_params].stage = SG_SHADERSTAGE_FRAGMENT;
    sd.uniform_blocks[UB_tc_pbr_fs_params].size = sizeof(tc_pbr_fs_params_t);
    const auto shader = sg_make_shader(sd);
    sg_pipeline_desc pd = {}; pd.shader = shader; pd.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    internal::PbrDrawCommand cmd = {}; cmd.pip = sg_make_pipeline(pd); cmd.vertexCount = 3;
    float vertices[9] = {}; sg_buffer_desc bd = {}; bd.data = {vertices,sizeof(vertices)};
    cmd.bind.vertex_buffers[0] = sg_make_buffer(bd);
    cmd.shadowGeneration = windows.main.shadow.storage; cmd.fsp.shadowMapParams[1] = 1;
    internal::ensureCurrentPass();
    setScissor(5,6,7,8); target.viewport = {9,10,11,12,true};
    internal::applyTargetDrawingState(); internal::parkWindowPass();
    struct Trace { int sx=0,sy=0,sw=0,sh=0,vx=0,vy=0,vw=0,vh=0,draws=0; bool ok=true; } trace;
    auto saved = _sg.hooks;
    sg_trace_hooks hooks = {}; hooks.user_data = &trace;
    hooks.begin_pass = [](const sg_pass*,void* u) {
        auto& t = *static_cast<Trace*>(u); t.sx=t.sy=t.vx=t.vy=0; t.sw=t.vw=256; t.sh=t.vh=192;
    };
    hooks.apply_scissor_rect = [](int x,int y,int w,int h,bool,void* u) {
        auto& t = *static_cast<Trace*>(u); t.sx=x;t.sy=y;t.sw=w;t.sh=h;
    };
    hooks.apply_viewport = [](int x,int y,int w,int h,bool,void* u) {
        auto& t = *static_cast<Trace*>(u); t.vx=x;t.vy=y;t.vw=w;t.vh=h;
    };
    hooks.draw = [](int,int,int,void* u) {
        auto& t = *static_cast<Trace*>(u); ++t.draws;
        t.ok &= t.sx==5 && t.sy==6 && t.sw==7 && t.sh==8 && t.vx==9 && t.vy==10 && t.vw==11 && t.vh==12;
    };
    sg_install_trace_hooks(&hooks);
    target.deferredPbrDraws.push_back({0,cmd}); target.advanceLayer();
    internal::consumeShadowSlot(0);
    target.deferredPbrDraws.push_back({0,cmd}); target.advanceLayer();
    internal::ensureCurrentPass(); target.flushLayers(); target.endPass();
    sg_install_trace_hooks(&saved);
    ok &= trace.ok && trace.draws == 2;
    sg_destroy_pipeline(cmd.pip); sg_destroy_buffer(cmd.bind.vertex_buffers[0]); sg_destroy_shader(shader);
    std::printf("SHADOW REPLAY scissor=5,6,7,8 viewport=9,10,11,12 draws=%d ok=%d\n",trace.draws,ok);
    return ok;
}

bool shadowNativeFailureProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    auto& a = windows.main; auto& b = windows.secondary.context();
    bool ok = internal::ensureShadowStorage(b,64);
    const auto foreign = b.shadow.storage;
    int errors = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("shadow use discarded") != std::string::npos) ++errors;
    });
    // Every generation resource, including all attachment views and the
    // sampling view, can fail natively despite a VALID dummy SG state.
    for (int stage = 0; stage < 9; ++stage) {
        ok &= internal::ensureShadowStorage(a,64);
        const auto old = a.shadow.storage->colorImage;
        struct Created { sg_image images[2] = {}; sg_view views[6] = {}; sg_sampler sampler = {}; int ni=0,nv=0; } made;
        auto saved = _sg.hooks; sg_trace_hooks hooks = {}; hooks.user_data = &made;
        hooks.make_image = [](const sg_image_desc*,sg_image h,void* u) { auto& c=*static_cast<Created*>(u); c.images[c.ni++]=h; };
        hooks.make_view = [](const sg_view_desc*,sg_view h,void* u) { auto& c=*static_cast<Created*>(u); c.views[c.nv++]=h; };
        hooks.make_sampler = [](const sg_sampler_desc*,sg_sampler h,void* u) { static_cast<Created*>(u)->sampler=h; };
        sg_install_trace_hooks(&hooks);
        a.shadow.count = 2; const auto failures = a.shadow.allocationFailures;
        nativeFailureWasValid = false; nativeChecksUntilFailure = stage;
        const bool rejected = !internal::ensureShadowStorage(a,128);
        nativeChecksUntilFailure = -1;
        sg_install_trace_hooks(&saved);
        ok &= rejected && nativeFailureWasValid && !a.shadow.storage && a.shadow.count == 0 &&
            a.shadow.allocationFailures == failures+1 && b.shadow.storage == foreign &&
            sg_query_image_state(foreign->colorImage) == SG_RESOURCESTATE_VALID;
        internal::endGpuFrame();
        ok &= sg_query_image_state(old) == SG_RESOURCESTATE_INVALID;
        for (int i=0;i<made.ni;++i) ok &= sg_query_image_state(made.images[i]) == SG_RESOURCESTATE_INVALID;
        for (int i=0;i<made.nv;++i) ok &= sg_query_view_state(made.views[i]) == SG_RESOURCESTATE_INVALID;
        if (made.sampler.id) ok &= sg_query_sampler_state(made.sampler) == SG_RESOURCESTATE_INVALID;
    }
    ok &= errors == 9 && internal::ensureShadowStorage(a,64);
    std::printf("SHADOW NATIVE null_objects=9 partial_retirement/foreign/recovery=%d errors=%d expected=9\n",ok,errors);
    return ok;
}

// Run after all drawing tests; deliberately start two fresh devices so native
// IDs recycle exactly, then exercise the real cleanup and retirement collector.
bool shadowDeviceRestartProbe() {
    cleanup();
    sg_desc desc = {}; sg_setup(&desc); sgl_desc_t sd = {}; sgl_setup(&sd);
    auto& owner = internal::mainWindowContext();
    bool ok = internal::ensureShadowStorage(owner,64);
    if (!owner.shadow.storage) return false;
    const auto old = owner.shadow.storage->colorImage;
    cleanup();
    sg_setup(&desc); sgl_setup(&sd);
    ok &= internal::ensureShadowStorage(owner,64);
    if (!owner.shadow.storage) return false;
    const auto fresh = owner.shadow.storage->colorImage;
    internal::drainPendingGpuDestroys();
    ok &= old.id == fresh.id && sg_query_image_state(fresh) == SG_RESOURCESTATE_VALID;
    std::printf("SHADOW RESTART old=%x fresh=%x live=%d ok=%d\n",old.id,fresh.id,sg_query_image_state(fresh),ok);
    cleanup();
    return ok;
}

// Exercise host allocation/retirement without allocating native image memory.
bool shadowOwnershipProbe() {
    DummyWindowPair windows;
    auto& a = windows.main;
    auto& b = windows.secondary.context();
    internal::restoreWindowContext(&a);
    bool ok = !a.shadow.storage && !b.shadow.storage;
    ok &= internal::ensureShadowStorage(a,64) && internal::ensureShadowStorage(b,128);
    const auto foreign = b.shadow.storage;
    const auto oldImage = a.shadow.storage->colorImage;
    const auto oldView = a.shadow.storage->colorTexture;
    std::weak_ptr<internal::ShadowStorage> old = a.shadow.storage;
    internal::PbrDrawCommand cmd = {};
    cmd.shadowGeneration = a.shadow.storage;
    cmd.bind.views[VIEW_tc_pbr_shadowMap] = oldView;
    cmd.bind.samplers[SMP_tc_pbr_shadowMapSmp] = a.shadow.storage->sampler;
    a.swapchainTarget.deferredPbrDraws.push_back({0,std::move(cmd)});
    ok &= internal::ensureShadowStorage(a,128) && a.shadow.count == 0;
    internal::restoreWindowContext(&b);
    internal::endGpuFrame();
    ok &= !old.expired() && sg_query_image_state(oldImage) == SG_RESOURCESTATE_VALID &&
        sg_query_view_state(oldView) == SG_RESOURCESTATE_VALID;
    a.swapchainTarget.deferredPbrDraws.clear();
    internal::endGpuFrame();
    ok &= old.expired() && sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID &&
        sg_query_view_state(oldView) == SG_RESOURCESTATE_INVALID;
    internal::restoreWindowContext(&a);
    int failures = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("shadow use discarded") != std::string::npos) ++failures;
    });
    // Image, view and sampler pool exhaustion fail at different construction
    // stages. Restore the pool before retiring partial generations.
    for (auto* pool : {&_sg.pools.image_pool,&_sg.pools.view_pool,&_sg.pools.sampler_pool}) {
        ok &= internal::ensureShadowStorage(a,64);
        a.shadow.count = 2;
        const int available = pool->queue_top;
        pool->queue_top = 0;
        const bool rejected = !internal::ensureShadowStorage(a,256);
        pool->queue_top = available;
        ok &= rejected && !a.shadow.storage && a.shadow.count == 0 && !a.shadow.inPass &&
            b.shadow.storage == foreign && sg_query_image_state(foreign->colorImage) == SG_RESOURCESTATE_VALID;
        internal::endGpuFrame();
    }
    ok &= internal::ensureShadowStorage(a,64);
    failAllocation = true;
    ok &= !internal::ensureShadowStorage(a,256) && !a.shadow.storage && a.shadow.count == 0;
    for (int extent : {0,-1,INT_MAX})
        ok &= !internal::ensureShadowStorage(a,extent) && !a.shadow.storage;
    ok &= failures == 7 && b.shadow.storage == foreign;
    size_t color,depth,total;
    ok &= internal::shadowImageLayout(1024,color,depth,total) && total == 20971520 &&
        !internal::shadowImageLayout(INT_MAX,color,depth,total);
    std::printf("SHADOW OWNERSHIP lazy/generation/pins/failure/foreign=%d errors=%d expected=7\n",ok,failures);
    return ok;
}

bool shadowSustainedAllocationProbe() {
    DummyWindowPair windows;
    auto& a = windows.main;
    auto& b = windows.secondary.context();
    internal::restoreWindowContext(&a);
    bool ok = internal::ensureShadowStorage(a,64) && internal::ensureShadowStorage(b,128);
    const auto foreign = b.shadow.storage;
    const auto foreignImage = foreign->colorImage;
    int errors = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("shadow use discarded") != std::string::npos) ++errors;
    });
    auto intact = [&] {
        return b.shadow.storage == foreign && sg_query_image_state(foreignImage) == SG_RESOURCESTATE_VALID;
    };
    // Growth fails before the new object or during shared_ptr control-block
    // creation. Keep OOM active through fail(), final pin release and collection.
    for (int successfulAllocations : {0,1}) {
        ok &= internal::ensureShadowStorage(a,64);
        const auto oldImage = a.shadow.storage->colorImage;
        auto pin = a.shadow.storage;
        a.shadow.count = 2;
        const auto before = a.shadow.allocationFailures;
        allocationsUntilFailure = successfulAllocations;
        ok &= !internal::ensureShadowStorage(a,256) && !a.shadow.storage && a.shadow.count == 0 &&
            a.shadow.allocationFailures == before + 1 && intact();
        pin.reset();
        internal::endGpuFrame(); // Cannot build reference closure yet; retry later.
        ok &= sg_query_image_state(oldImage) == SG_RESOURCESTATE_VALID;
        allocationsUntilFailure = -1;
        internal::endGpuFrame();
        ok &= sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID && intact();
    }
    // Fail after the color image exists: the native partial generation and the
    // owner's old generation must both retire without allocating.
    ok &= internal::ensureShadowStorage(a,64);
    const auto oldImage = a.shadow.storage->colorImage;
    sg_image partial = {};
    auto hooks = _sg.hooks;
    auto injectedHooks = hooks;
    injectedHooks.make_image = [](const sg_image_desc*, sg_image image, void* user) {
        *static_cast<sg_image*>(user) = image;
        allocationsUntilFailure = 0;
        _sg.pools.view_pool.queue_top = 0;
    };
    injectedHooks.user_data = &partial;
    const int availableViews = _sg.pools.view_pool.queue_top;
    sg_install_trace_hooks(&injectedHooks);
    ok &= !internal::ensureShadowStorage(a,256) && !a.shadow.storage && intact();
    _sg.pools.view_pool.queue_top = availableViews;
    sg_install_trace_hooks(&hooks);
    ok &= partial.id && allocationsUntilFailure == 0 && errors == 3;
    allocationsUntilFailure = -1;
    internal::endGpuFrame();
    ok &= sg_query_image_state(partial) == SG_RESOURCESTATE_INVALID &&
        sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID;
    // Real WindowContext close with pending PBR is also noexcept under OOM.
    ok &= internal::ensureShadowStorage(a,64);
    const auto closeImage = a.shadow.storage->colorImage;
    internal::PbrDrawCommand cmd = {};
    cmd.shadowGeneration = a.shadow.storage;
    a.swapchainTarget.deferredPbrDraws.push_back({0,std::move(cmd)});
    allocationsUntilFailure = 0;
    a.teardownTargets();
    ok &= !a.shadow.storage && a.swapchainTarget.deferredPbrDraws.empty() && intact() && errors == 3;
    allocationsUntilFailure = -1;
    internal::endGpuFrame();
    ok &= sg_query_image_state(closeImage) == SG_RESOURCESTATE_INVALID &&
        internal::ensureShadowStorage(a,64) && intact() && rejectedAllocations >= 4;
    std::printf("SHADOW SUSTAINED growth/control_block/partial/pin/close/recovery=%d errors=%d expected=3 rejected=%d\n",
        ok,errors,rejectedAllocations);
    return ok;
}

bool parkedShadowProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    auto& sh = windows.main.shadow;
    auto& target = sh.target;
    sg_image_desc im = {}; im.width = im.height = 16;
    im.pixel_format = SG_PIXELFORMAT_RGBA8; im.usage.color_attachment = true;
    const auto image = sg_make_image(im);
    sg_view_desc vd = {}; vd.color_attachment.image = image;
    const auto view = sg_make_view(vd);
    sg_shader_desc sd = {};
    sd.uniform_blocks[UB_tc_shadow_shadow_vs_params].stage = SG_SHADERSTAGE_VERTEX;
    sd.uniform_blocks[UB_tc_shadow_shadow_vs_params].size = sizeof(tc_shadow_shadow_vs_params_t);
    const auto shader = sg_make_shader(sd);
    sg_pipeline_desc pd = {}; pd.shader = shader;
    pd.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pd.index_type = SG_INDEXTYPE_UINT32;
    pd.depth.pixel_format = SG_PIXELFORMAT_NONE;
    target.passKind = internal::TargetPassKind::Fixed; target.isFbo = true;
    target.width = target.height = 16; target.colorFormat = SG_PIXELFORMAT_RGBA8;
    target.sampleCount = 1; target.hasDepth = false; target.depthFormat = SG_PIXELFORMAT_NONE;
    target.binding.attachments.colors[0] = view;
    target.hasFixedPipeline = true;
    target.fixedPipeline = internal::PipelineKey::raw(internal::PipelineRole::Shader,pd);
    target.pooledResources = internal::leaseTargetResources(SG_PIXELFORMAT_RGBA8,1,SG_PIXELFORMAT_NONE);
    Mesh mesh; mesh.addVertex(0,0,0).addVertex(1,0,0).addVertex(0,1,0);
    int prefix = 0, suffix = 0;
    bool childRejected = false, resumed = false;
    {
        internal::TargetPassScope scope(target);
        sh.inPass = true; sh.current = 0;
        const int start = submittedDraws;
        shadowDraw(mesh);
        prefix = submittedDraws - start;
        internal::restoreWindowContext(&windows.secondary.context()); present();
        internal::restoreWindowContext(nullptr);
        const int beforeSuffix = submittedDraws;
        shadowDraw(mesh);
        internal::getPbrPipeline().shadowDrawMesh(mesh);
        suffix = submittedDraws - beforeSuffix;
        resumed = target.inPass && passes.back().action.colors[0].load_action == SG_LOADACTION_LOAD;
        Fbo child; child.allocate(8,8); child.begin(0,0,0);
        const auto beforeChild = submittedDraws;
        shadowDraw(mesh);
        childRejected = submittedDraws == beforeChild && !target.inPass;
        child.end();
        sh.inPass = false;
    }
    present();
    sg_destroy_view(view); sg_destroy_image(image); sg_destroy_shader(shader);
    std::printf("PARKED SHADOW prefix=%d suffix_after_park=%d LOAD=%d child_rejected=%d expected=1/2/1/1\n",prefix,suffix,resumed,childRejected);
    return prefix == 1 && suffix == 2 && resumed && childRejected;
}
bool foreignTeardownReplayFailureProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    Shader shader;
    bool ok = shader.load(descriptor);
    internal::ensureCurrentPass();
    shader.begin(); submit(shader);
    auto& target = internal::currentTarget();
    const auto token = target.activation;
    const auto before = submittedDraws;
    failAllocation = true;
    windows.secondary.context().teardownTargets();
    ok &= !failAllocation && !target.inPass && !_sg.cur_pass.in_pass && target.recordingFailed &&
        target.deferredShaderDraws.empty() && !target.openShaderScopes && windows.main.shaderStack.empty() &&
        !sgl_tc_context_has_pending_draws(target.resources().context) && target.activation == token &&
        &internal::currentWindowContext() == &windows.main && submittedDraws == before;
    // The owner boundary completes the discarded recording before new work.
    present();
    ok &= submittedDraws == before && !target.recordingFailed;
    // Recovery permits a new pass, with neither a stale draw nor a scope.
    internal::ensureCurrentPass(); recordTriangle(); present();
    ok &= submittedDraws == before + 1 && !target.recordingFailed;
    std::printf("FOREIGN TEARDOWN replay_allocation_failure_cleanup=%d expected=1\n",ok);
    return ok;
}

// Same production scope used by Metal captureWindow, including calls nested
// inside present() and a foreign tick. The dummy backend cannot test GPU waits.
bool synchronousCapturePhaseProbe(bool secondary) {
    DummyWindowPair windows;
    auto* owner = secondary ? &windows.secondary.context() : &windows.main;
    auto* foreign = secondary ? &windows.main : &windows.secondary.context();
    internal::restoreWindowContext(owner);
    auto& target = internal::currentTarget();
    const auto token = target.activation;
    const auto epoch = owner->renderEpoch;
    const auto beforeCommits = commits;
    const auto beforeDraws = submittedDraws;
    beginFrame(); clear(1,0,0); setColor(1); drawRect(8,8,16,16);
    bool ok = !target.inPass && !target.passStartedThisFrame;
    const auto firstPassIndex = passes.size();
    auto capture = [&] {
        const auto before = commits;
        {
            internal::CapturePrefixScope phase;
            ok &= !target.inPass && !_sg.cur_pass.in_pass && commits == before &&
                target.activation == token && owner->renderEpoch == epoch;
        }
        ok &= target.inPass && passes.back().action.colors[0].load_action == SG_LOADACTION_LOAD;
    };
    capture(); // draw() phase
    ok &= submittedDraws == beforeDraws + 1 && !sgl_tc_context_has_pending_draws(target.resources().context);
    std::printf("PREFIX PROBE secondary=%d after scope draws=%d pending=%d expected_draws=1\n",
        secondary, submittedDraws - beforeDraws, sgl_tc_context_has_pending_draws(target.resources().context));
    ok &= passes.size() == firstPassIndex + 2;
    if (passes.size() > firstPassIndex) {
        const auto& firstPass = passes[firstPassIndex];
        ok &= firstPass.action.colors[0].load_action == SG_LOADACTION_CLEAR &&
            firstPass.action.colors[0].clear_value.r == 1 && firstPass.action.colors[0].clear_value.g == 0;
    }
    recordTriangle();
    internal::restoreWindowContext(foreign); internal::ensureCurrentPass(); recordTriangle();
    auto& foreignTarget = internal::currentTarget();
    internal::restoreWindowContext(owner); // parks the other window's pass
    ok &= !foreignTarget.inPass && !target.inPass;
    const auto emptyParkedPasses = passes.size();
    {
        internal::CapturePrefixScope phase;
    }
    ok &= !target.inPass && !foreignTarget.inPass && passes.size() == emptyParkedPasses;
    setColor(1); drawRect(40,8,16,16); // new CPU work on the lazy parked target
    const auto beforeParkedCapture = submittedDraws;
    capture();
    ok &= !foreignTarget.inPass && submittedDraws == beforeParkedCapture + 1 &&
        !sgl_tc_context_has_pending_draws(target.resources().context);
    {
        internal::CapturePrefixScope phase;
        phase.leaveParked(); // native capture failure/timeout
    }
    ok &= !target.inPass && !_sg.cur_pass.in_pass && target.activation == token;
    recordTriangle();
    int renderCaptures = 0;
    auto listener = events().onRender.listen([&] { ++renderCaptures; capture(); recordTriangle(); });
    present();
    ok &= renderCaptures == 1 && commits == beforeCommits + 1 && submittedDraws == beforeDraws + 6 &&
        owner->renderEpoch == epoch + 1;
    const auto finishedPasses = passes.size();
    {
        internal::CapturePrefixScope phase; // completed afterFrame source
    }
    ok &= passes.size() == finishedPasses && !target.inPass && !owner->framePending;
    beginFrame(); clear(0,0,1); // the following frame has only a pending clear
    {
        internal::CapturePrefixScope phase;
        ok &= !target.inPass && passes.back().action.colors[0].load_action == SG_LOADACTION_CLEAR &&
            passes.back().action.colors[0].clear_value.b == 1 && submittedDraws == beforeDraws + 6;
        phase.leaveParked();
    }
    std::printf("SYNC CAPTURE PHASE secondary=%d draw_onRender_foreign_LOAD=%d expected=1\n", secondary,ok);
    return ok;
}

bool begunResizeProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    auto& target = internal::currentTarget();
    const auto context = target.resources().context;
    const auto generation = target.resources().generation;
    int oldDrawable = 1, oldDepth = 2, oldMsaa = 3;
    int newDrawable = 4, newDepth = 5, newMsaa = 6;
    auto acquired = dummySwapchain(nullptr);
    acquired.metal.current_drawable = &oldDrawable;
    acquired.metal.depth_stencil_texture = &oldDepth;
    acquired.metal.msaa_color_texture = &oldMsaa;
    windows.main.acquireSwapchainUser = &acquired;
    windows.main.acquireSwapchain = [](void* value) { return *static_cast<sg_swapchain*>(value); };
    submittedDraws = 0;
    internal::ensureCurrentPass(); recordTriangle(); target.flushLayers();
    bool ok = target.passStartedThisFrame && !sgl_tc_context_has_pending_draws(context);
    internal::restoreWindowContext(&windows.secondary.context());
    // Model native resize replacing the attachment generation while A is
    // parked. Dummy pointers test descriptor identity, not Objective-C retain.
    acquired.metal.current_drawable = &newDrawable;
    acquired.metal.depth_stencil_texture = &newDepth;
    acquired.metal.msaa_color_texture = &newMsaa;
    acquired.width = 128; acquired.height = 96;
    internal::resizeSgl(3072,377);
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES + 3; ++i) { beginFrame(); present(); }
    ok &= internal::sglBudget().pendingResize == 3072 && target.resources().context.id == context.id;
    internal::restoreWindowContext(nullptr); beginFrame();
    ok &= target.resources().generation == generation;
    internal::ensureCurrentPass();
    ok &= passes.back().action.colors[0].load_action == SG_LOADACTION_LOAD;
    ok &= passes.back().swapchain.metal.current_drawable == &oldDrawable &&
        passes.back().swapchain.metal.depth_stencil_texture == &oldDepth &&
        passes.back().swapchain.metal.msaa_color_texture == &oldMsaa &&
        passes.back().swapchain.width == 256 && passes.back().swapchain.height == 192;
    recordTriangle(); present();
    ok &= submittedDraws == 2;
    beginFrame();
    auto* selected = _sgl_lookup_context(target.resources().context.id);
    ok &= !internal::sglBudget().pendingResize && selected->desc.max_vertices == 3072 && selected->desc.max_commands == 377;
    internal::ensureCurrentPass();
    ok &= passes.back().swapchain.metal.current_drawable == &newDrawable &&
        passes.back().swapchain.metal.depth_stencil_texture == &newDepth &&
        passes.back().swapchain.metal.msaa_color_texture == &newMsaa;
    present();
    windows.main.acquireSwapchain = dummySwapchain;
    windows.main.acquireSwapchainUser = nullptr;
    std::printf("BEGUN FRAME RESIZE deferred=1 LOAD=1 retained_attachments=1 draws=%d expected=2 capacities=3072/377 ok=%d\n",submittedDraws,ok);
    return ok;
}

bool resourceRetirementProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(nullptr);
    sg_image_desc imageDesc = {}; imageDesc.width = imageDesc.height = 2;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDesc.usage.dynamic_update = true;
    const auto image = sg_make_image(imageDesc);
    sg_view_desc viewDesc = {}; viewDesc.texture.image = image;
    const auto view = sg_make_view(viewDesc);
    const auto sampler = sg_make_sampler(sg_sampler_desc{});
    sgl_enable_texture(); sgl_texture(view, sampler); recordTriangle(); sgl_disable_texture();
    internal::deferGpuDestroy(view); internal::deferGpuDestroy(image); internal::deferGpuDestroy(sampler);
    internal::restoreWindowContext(&windows.secondary.context());
    present(); // Warm B's caches; A's referenced resources remain pending.
    sg_buffer_desc bufferDesc = {}; bufferDesc.size = 16; bufferDesc.usage.vertex_buffer = true; bufferDesc.usage.dynamic_update = true;
    const auto unrelated = sg_make_buffer(bufferDesc);
    internal::deferGpuDestroy(unrelated);
    failAllocation = true;
    internal::drainPendingGpuDestroys();
    bool ok = sg_query_buffer_state(unrelated) == SG_RESOURCESTATE_VALID;
    internal::drainPendingGpuDestroys(); // Recover and retire only unrelated work.
    for (int i = 0; i < 3; ++i) present();
    ok &=  sg_query_view_state(view) == SG_RESOURCESTATE_VALID &&
        sg_query_image_state(image) == SG_RESOURCESTATE_VALID && sg_query_sampler_state(sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(unrelated) != SG_RESOURCESTATE_VALID;
    internal::restoreWindowContext(nullptr);
    present();
    ok &= sg_query_view_state(view) != SG_RESOURCESTATE_VALID &&
        sg_query_image_state(image) != SG_RESOURCESTATE_VALID && sg_query_sampler_state(sampler) != SG_RESOURCESTATE_VALID;
    std::printf("RECORDING REFERENCES related_retained=1 unrelated_retired=1 consumed_retired=1 ok=%d\n",ok);
    return ok;
}

bool growthFailureProbe() {
    const auto context = sgl_make_context(sgl_context_desc_t{});
    auto* ctx = _sgl_lookup_context(context.id);
    bool ok = _sgl_checked_capacity(INT_MAX, INT_MAX, 1, sizeof(_sgl_vertex_t)) == 0 &&
        _sgl_checked_capacity(4,4,1,SIZE_MAX) == 0 &&
        _sgl_needed_gpu_vertices(SIZE_MAX, 40) == 0 && _sgl_needed_gpu_vertices(INT_MAX, 40) == 0;
    const int oldCap = ctx->vertices.cap, oldNext = ctx->vertices.next;
    ctx->vertices.cap = ctx->vertices.next = INT_MAX;
    ok &= !_sgl_next_vertex(ctx) && ctx->error.vertices_full;
    ctx->vertices.cap = oldCap; ctx->vertices.next = oldNext;
    const auto allocator = _sgl.desc.allocator;
    _sgl.desc.allocator.alloc_fn = [](size_t,void*) -> void* { return nullptr; };
    ctx->vertices.next = ctx->vertices.cap;
    ok &= !_sgl_next_vertex(ctx) && ctx->error.any;
    ctx->vertices.next = oldNext;
    _sgl.desc.allocator = allocator;
    sgl_destroy_context(context);
    std::printf("RECORDING GROWTH representation_and_allocation_failure=%d\n",ok);
    return ok;
}

bool rawFailureProbe() {
    DummyWindowPair windows;
    internal::restoreWindowContext(&windows.secondary.context()); recordTriangle();
    const auto foreign = windows.secondary.context().swapchainTarget.resources().context;
    internal::restoreWindowContext(nullptr); recordTriangle();
    auto& target = internal::currentTarget();
    target.sglLayerNext = INT_MAX;
    Shader shader; bool ok = shader.load(descriptor);
    shader.begin();
    ok &= target.recordingFailed && !target.openShaderScopes && target.deferredShaderDraws.empty() &&
        !sgl_tc_context_has_pending_draws(target.resources().context) && sgl_tc_context_has_pending_draws(foreign);
    present();
    internal::DeferredShaderDraw draw;
    target.deferredShaderDraws.shrink_to_fit();
    failAllocation = true;
    ok &= !internal::appendTargetRecording(target, target.deferredShaderDraws, std::move(draw)) && target.recordingFailed;
    ok &= sgl_tc_context_has_pending_draws(foreign);
    present();
    internal::restoreWindowContext(&windows.secondary.context());
    submittedDraws = 0; present(); ok &= submittedDraws == 1;
    std::printf("RAW FAILURE layer_overflow_and_allocation=1 foreign_draws=%d expected=1 ok=%d\n",submittedDraws,ok);
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
    if (argc == 2 && std::strcmp(argv[1],"--native-allocation-check") == 0) {
        const auto executable = fs::read_symlink("/proc/self/exe").parent_path() / "shadowAllocationGl";
        execl(executable.c_str(),executable.c_str(),"--gpu-check",static_cast<char*>(nullptr));
        std::perror("shadowAllocationGl exec");
        return 2;
    }
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
    const bool parity = noDrawParityProbe(false,true) && noDrawParityProbe(true,true) && noDrawParityProbe(false,false);
    const bool rawKinds = rawKindsLifetimeProbe();
    const bool parking = parkedWindowProbe(false) && parkedWindowProbe(true);
    const bool shadowOwnership = shadowOwnershipProbe();
    const bool shadowReplay = shadowReplayDrawingStateProbe();
    const bool shadowNative = shadowNativeFailureProbe();
    const bool shadowSustained = shadowSustainedAllocationProbe();
    const bool shadowParking = parkedShadowProbe();
    const bool capturePhase = synchronousCapturePhaseProbe(false) && synchronousCapturePhaseProbe(true);
    const bool foreignTeardown = foreignTeardownReplayFailureProbe();
    const bool begunResize = begunResizeProbe();
    const bool retirement = resourceRetirementProbe();
    const bool growth = growthFailureProbe();
    const bool rawFailure = rawFailureProbe();
    const bool destruction = activeDestructionProbe();
    const bool shadowRestart = shadowDeviceRestartProbe();
    const int result=shadowReplay && shadowNative && shadowRestart && teardown && inherited && replay && normal && flushed && failedLease && stores && pipelines && lazyResize && tickRestore && interleaved && parity && rawKinds && parking && shadowOwnership && shadowSustained && shadowParking && capturePhase && foreignTeardown && begunResize && retirement && growth && rawFailure && destruction ? 0 : 1;
    return result;
}
#else
#include <cstdio>
int main() {
    std::puts("SKIP: ELF dummy-backend override probe runs on Linux; use renderPassStack --gpu-check on this backend");
    return 0;
}
#endif
