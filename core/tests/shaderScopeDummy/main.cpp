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
        const bool correct = role == ((depth ? 0x1000u : 0u) | unsigned(BlendMode::Add)) &&
            appliedPipeline.depth.write_enabled == depth &&
            appliedPipeline.depth.compare == (depth ? SG_COMPAREFUNC_LESS_EQUAL : SG_COMPAREFUNC_ALWAYS) &&
            appliedPipeline.colors[0].blend.src_factor_rgb == SG_BLENDFACTOR_SRC_ALPHA &&
            appliedPipeline.colors[0].blend.dst_factor_rgb == SG_BLENDFACTOR_ONE;
        std::printf("INHERITED PIPELINE depth=%d role=0x%x descriptor=%d expected=1\n",depth,role,correct);
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
    auto* lease = internal::leaseTargetResources(SG_PIXELFORMAT_RGBA8,1);
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
    auto* lease = internal::leaseTargetResources(SG_PIXELFORMAT_RGBA8,1);
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
    const bool destruction = activeDestructionProbe();
    return teardown && inherited && replay && normal && flushed && failedLease && stores && destruction ? 0 : 1;
}
#else
#include <cstdio>
int main() {
    std::puts("SKIP: ELF dummy-backend override probe runs on Linux; use renderPassStack --gpu-check on this backend");
    return 0;
}
#endif
