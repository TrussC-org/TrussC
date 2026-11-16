#if defined(__linux__) && !defined(__ANDROID__)
#undef SOKOL_GLCORE
#undef SOKOL_GLES3
#define SOKOL_DUMMY_BACKEND
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL
#define SOKOL_GFX_IMPL
#include "sokol/sokol_gfx.h"
#undef SOKOL_GFX_IMPL
#define SOKOL_GL_IMPL
#include "sokol/util/sokol_gl_tc.h"
#undef SOKOL_GL_IMPL
#include <TrussC.h>
#include "tc/app/internal/tcPipelinePoolLog.h"
#include <cstdio>
using namespace tc;
namespace trussc::internal {
PipelinePoolLog& installPipelinePoolLogging();
sg_shader sglPremultShader() { return {}; }
sg_shader sglCoverageShader() { return {}; }
sg_shader sglCoveragePremultShader() { return {}; }
}
namespace {
int failures = 0;
double now = 0;
std::vector<LogEventArgs> messages;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
int count(LogLevel level, const std::string& text = "") {
    return int(std::count_if(messages.begin(), messages.end(), [&](const auto& event) {
        return event.level == level && event.message.find(text) != std::string::npos;
    }));
}
internal::PipelinePoolLog& start(int sgSize, int sglSize) {
    messages.clear();
    sg_desc desc = {};
    desc.pipeline_pool_size = sgSize;
    desc.logger.func = internal::sokolLog;
    sg_setup(&desc);
    auto& monitor = internal::installPipelinePoolLogging();
    monitor.clock = [] { return now; };
    auto gl = internal::sglSetupDescriptor();
    gl.pipeline_pool_size = sglSize;
    sgl_setup(&gl);
    internal::samplePipelinePools();
    return monitor;
}
void stop() { sgl_shutdown(); sg_shutdown(); }
// Explicit event sampling, just as at TrussC's pipeline call sites.
sg_pipeline allocate() {
    auto pipeline = sg_alloc_pipeline();
    internal::samplePipelinePools();
    return pipeline;
}
void release(sg_pipeline pipeline) {
    sg_dealloc_pipeline(pipeline);
    internal::samplePipelinePools();
}
void destroy(sg_pipeline pipeline) {
    sg_destroy_pipeline(pipeline);
    internal::samplePipelinePools();
}
sgl_pipeline makeImmediate(const sg_pipeline_desc* desc) {
    auto pipeline = sgl_make_pipeline(desc);
    internal::samplePipelinePools();
    return pipeline;
}
void destroyImmediate(sgl_pipeline pipeline) {
    sgl_destroy_pipeline(pipeline);
    internal::samplePipelinePools();
}
void recordPrimitive(sgl_pipeline pipeline, int primitive) {
    sgl_load_pipeline(pipeline);
    switch (primitive) {
        case 0: sgl_begin_points(); break;
        case 1: sgl_begin_lines(); break;
        case 2: sgl_begin_line_strip(); break;
        case 3: sgl_begin_triangles(); break;
        case 4: sgl_begin_triangle_strip(); break;
    }
    sgl_v2f(0, 0);
    sgl_v2f(1, 0);
    sgl_v2f(0, 1);
    sgl_end();
}
}
int main() {
    auto listener = getLogger().onLog.listen([](LogEventArgs& event) { messages.push_back(event); });
    auto& monitor = start(20, 20); // unused default sgl pipeline occupies no sg slots
    check("no messages below 50%", messages.empty());
    std::vector<sg_pipeline> held;
    sg_pipeline_desc lazyDesc = {};
    std::vector<sgl_pipeline> lazy;
    for (int i = 0; i < 5; ++i) lazy.push_back(makeImmediate(&lazyDesc));
    check("CPU recipes do not reach either threshold", messages.empty());
    auto recordChild = [&](int i) { recordPrimitive(lazy[i / 5], i % 5); };
    for (int i = 0; i < 9; ++i) recordChild(i);
    check("lazy SG children below 50% are quiet", messages.empty() && sg_query_stats().total.pipelines.alive == 9);
    recordChild(9);
    const std::string remedy = "; raise it with WindowSettings::setPipelinePoolSize() before runApp()";
    check("50% Notice once and exact text", count(LogLevel::Notice, "sg pipeline pool 50% used (10/20)" + remedy) == 1);
    for (int i = 10; i < 15; ++i) recordChild(i);
    check("lazy SG children below 80% have no Warning or Error", count(LogLevel::Warning) == 0 && count(LogLevel::Error) == 0);
    recordChild(15);
    check("80% Warning once and exact text", count(LogLevel::Warning, "sg pipeline pool 80% used (16/20)" + remedy) == 1);
    check("both thresholds precede any failure", count(LogLevel::Error) == 0 && monitor.exhaustionEvents == 0);
    for (int i = 16; i < 20; ++i) recordChild(i);
    const auto messageCount = messages.size();
    for (int i = 0; i < 20; ++i) recordChild(i);
    check("cached children allocate nothing and emit no new messages", messages.size() == messageCount && sg_query_stats().total.pipelines.alive == 20);
    for (int i = 0; i < 11; ++i) recordPrimitive(lazy[4], 0);
    check("full Error once and exact text", count(LogLevel::Error, "sg pipeline pool 100% used (20/20)" + remedy) == 1);
    now = 1;
    allocate();
    check("next second reports suppressed count", count(LogLevel::Error, "; 10 failures suppressed") == 1);
    check("one Error per second", count(LogLevel::Error) == 2);
    for (auto pipeline : lazy) destroyImmediate(pipeline);
    for (int i = 0; i < 16; ++i) held.push_back(allocate());
    check("Notice stays once; Warning repeats on recross", count(LogLevel::Notice) == 1 && count(LogLevel::Warning) == 2);
    for (auto pipeline : held) destroy(pipeline);
    held.clear();
    // Dropping below 50% and crossing again must never repeat Notice.
    for (int i = 0; i < 10; ++i) held.push_back(allocate());
    check("destroy and recross still keeps Notice once", count(LogLevel::Notice) == 1);
    for (auto pipeline : held) destroy(pipeline);

    // Real coordinator creation paths: full pool suppresses high-level Warning.
    internal::RenderTarget target;
    target.colorFormat = SG_PIXELFORMAT_RGBA8; target.sampleCount = 1;
    target.resources().context = sgl_default_context();
    sg_shader_desc shaderDesc = {};
    const auto shader = sg_make_shader(&shaderDesc);
    sg_pipeline_desc pd = {}; pd.shader = shader; pd.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    held.clear();
    while (sg_query_stats().total.pipelines.free) held.push_back(allocate());
    messages.clear(); now = 2;
    auto result = target.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader, pd));
    check("raw exhaustion has exactly one Error", !result.raw.id && count(LogLevel::Error) == 1 && count(LogLevel::Warning) == 0);
    messages.clear(); now = 3;
    auto immediate = target.pipeline(internal::PipelineKey::immediate({}));
    check("immediate recipe does not allocate SG children", immediate.immediate.id && messages.empty());
    sgl_load_pipeline(immediate.immediate);
    sgl_begin_triangles(); sgl_v2f(0, 0); sgl_v2f(1, 0); sgl_v2f(0, 1); sgl_end();
    check("immediate first-use exhaustion has exactly one Error", count(LogLevel::Error) == 1 && count(LogLevel::Warning) == 0);
    // Exercise the production compute wrapper with a valid dummy descriptor.
    _sg.features.compute = true;
    messages.clear(); now = 4;
    {
        ComputeShader compute;
        const auto descriptor = +[](sg_backend) -> const sg_shader_desc* {
            static sg_shader_desc desc = {};
            desc.compute_func.source = "dummy compute";
            return &desc;
        };
        const bool first = compute.load(descriptor);
        const bool repeated = compute.load(descriptor);
        check("compute exhaustion and repeat emit only one rate-limited Error",
              !first && !repeated && messages.size() == 1 &&
              count(LogLevel::Error, "sg pipeline pool 100% used (20/20)" + remedy) == 1);
    }
    for (auto pipeline : held) destroy(pipeline);
    messages.clear();
    pd.shader = {}; pd.colors[0].blend.enabled = true;
    const auto before = monitor.exhaustionEvents;
    target.pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader, pd));
    check("bad shader preserves existing Warning", count(LogLevel::Warning, "could not create raster pipeline; affected draws skipped until rebuild") == 1 && monitor.exhaustionEvents == before && count(LogLevel::Error) == 0);
    target.resources().release();
    sg_destroy_shader(shader);
    stop();

    start(100, 10);
    check("sgl below 50% is quiet", messages.empty());
    sg_pipeline_desc desc = {};
    std::vector<sgl_pipeline> sglHeld;
    for (int i = 0; i < 9; ++i) sglHeld.push_back(makeImmediate(&desc));
    check("Notice stays once across graphics restart and pool change", count(LogLevel::Notice) == 0);
    check("sgl 80% Warning", count(LogLevel::Warning, "sokol_gl pipeline pool 80% used (8/10)" + remedy) == 1);
    messages.clear(); now = 4.5;
    check("sgl full fails across restart", !makeImmediate(&desc).id);
    check("restart preserves Error rate gate", count(LogLevel::Error) == 0);
    now = 5;
    check("sgl full fails", !makeImmediate(&desc).id);
    check("sgl exhaustion exactly one Error", count(LogLevel::Error, "sokol_gl pipeline pool 100% used (10/10)" + remedy) == 1 && messages.size() == 1);
    check("restart preserves suppressed count", count(LogLevel::Error, "; 2 failures suppressed") == 1);
    for (auto pipeline : sglHeld) destroyImmediate(pipeline);
    sglHeld.clear(); messages.clear();
    for (int i = 0; i < 7; ++i) sglHeld.push_back(makeImmediate(&desc));
    check("sgl recross repeats only Warning", count(LogLevel::Notice) == 0 && count(LogLevel::Warning) == 1);
    for (auto pipeline : sglHeld) destroyImmediate(pipeline);
    stop();
    return failures ? 1 : 0;
}
#else
#include <cstdio>
int main() { std::puts("SKIP: real-coordinator dummy override requires Linux ELF"); return 0; }
#endif
