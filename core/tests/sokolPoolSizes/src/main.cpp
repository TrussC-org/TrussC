#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>
#include <type_traits>
using namespace tc;
namespace {
static_assert(std::is_aggregate_v<WindowSettings>, "Preserve aggregate initialization of existing settings");
int failures = 0;
bool completed = false;
int resourceCount = 64, shaderCount = 64, pipelineCount = 256;
std::vector<LogEventArgs> messages;
void check(const char* label, bool ok) {
    std::printf("sokolPoolSizes %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
template<class Allocate, class Release>
void exhaust(Allocate allocate, Release release, int capacity, const char* name, const char* setter) {
    using Handle = decltype(allocate());
    std::vector<Handle> handles;
    for (int i = 0; i < capacity + 2; ++i) {
        auto handle = allocate();
        if (handle.id) handles.push_back(handle);
    }
    check("pool reaches capacity", !handles.empty() && handles.size() <= static_cast<size_t>(capacity));
    for (auto handle : handles) release(handle);
    // A second exhaustion after slots become free must not repeat the Error.
    handles.clear();
    for (int i = 0; i < capacity + 2; ++i) {
        auto handle = allocate();
        if (handle.id) handles.push_back(handle);
    }
    for (auto handle : handles) release(handle);
    std::string expected = std::string(name) + " pool exhausted (" + std::to_string(capacity) + " " + name +
        "s in use); raise it with WindowSettings::" + setter + "() before runApp()";
    int errors = 0, verbose = 0;
    for (const auto& event : messages) {
        if (event.level == LogLevel::Error && event.message == expected) ++errors;
        if (event.level == LogLevel::Verbose && event.message.find(std::string(name) + " pool exhausted") != std::string::npos) ++verbose;
    }
    check(expected.c_str(), errors == 1);
    // Release sokol builds supply IDs instead of a message; either form is kept.
    if (!verbose) for (const auto& event : messages)
        if (event.level == LogLevel::Verbose && event.message.find("[sg] id:") == 0) ++verbose;
    check("original sokol lines kept at debug verbosity", verbose >= 2);
}
class PoolApp : public App {
public:
    void setup() override {
        auto desc = sg_query_desc();
        check("sg_setup receives all resource counts", desc.buffer_pool_size == resourceCount &&
            desc.image_pool_size == resourceCount && desc.view_pool_size == resourceCount && desc.sampler_pool_size == resourceCount);
        check("sg_setup receives shader and pipeline counts", desc.shader_pool_size == shaderCount && desc.pipeline_pool_size == pipelineCount);
        check("shared sgl setup descriptor follows pipeline setting", internal::sglSetupDescriptor().pipeline_pool_size == pipelineCount);
        // Changing the requested values after startup must not change the
        // descriptor used when sgl is rebuilt (or the counts in diagnostics).
        WindowSettings later;
        later.setResourcePoolSize(8).setShaderPoolSize(8).setPipelinePoolSize(8);
        internal::configureGraphicsPools(later);
        check("sgl rebuild retains startup pipeline count", internal::sglSetupDescriptor().pipeline_pool_size == pipelineCount);
        // Only allocated slots: no native GPU objects or shader compilation needed.
        exhaust(sg_alloc_buffer, sg_dealloc_buffer, resourceCount, "buffer", "setResourcePoolSize");
        exhaust(sg_alloc_image, sg_dealloc_image, resourceCount, "image", "setResourcePoolSize");
        exhaust(sg_alloc_view, sg_dealloc_view, resourceCount, "view", "setResourcePoolSize");
        exhaust(sg_alloc_sampler, sg_dealloc_sampler, resourceCount, "sampler", "setResourcePoolSize");
        exhaust(sg_alloc_shader, sg_dealloc_shader, shaderCount, "shader", "setShaderPoolSize");
        exhaust(sg_alloc_pipeline, sg_dealloc_pipeline, pipelineCount, "pipeline", "setPipelinePoolSize");
        const auto before = messages.size();
        // The separate sgl pool normally cannot fill before sg (up to five sg
        // slots per sgl slot). Exercise its logger event directly, including the
        // release-build path without a message.
        internal::sokolLog("sgl", 1, SGL_LOGITEM_PIPELINE_POOL_EXHAUSTED, nullptr, 1, nullptr, nullptr);
        internal::sokolLog("sgl", 1, SGL_LOGITEM_PIPELINE_POOL_EXHAUSTED, nullptr, 1, nullptr, nullptr);
        std::string expected = "pipeline pool exhausted (" + std::to_string(pipelineCount) +
            " pipelines in use); raise it with WindowSettings::setPipelinePoolSize() before runApp()";
        check("sgl has its own once gate and configured count", messages.size() == before + 3 &&
            messages[before].level == LogLevel::Error && messages[before].message == expected &&
            messages[before+1].level == LogLevel::Verbose && messages[before+2].level == LogLevel::Verbose);
        completed = true;
    }
    void update() override { exitApp(); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc != 2) {
        std::printf("SKIP: sokolPoolSizes needs --gpu-check, --large-check, --zero-check or --negative-check\n");
        return 0;
    }
    bool zero = std::strcmp(argv[1], "--zero-check") == 0;
    bool negative = std::strcmp(argv[1], "--negative-check") == 0;
    bool large = std::strcmp(argv[1], "--large-check") == 0;
    if (!zero && !negative && !large && std::strcmp(argv[1], "--gpu-check") != 0) return 1;
    if (large) { resourceCount = 50000; shaderCount = pipelineCount = 4096; }
    if (zero || negative) { resourceCount = 10000; shaderCount = pipelineCount = 1024; }
    auto listener = getLogger().onLog.listen([](LogEventArgs& event) { messages.push_back(event); });
    WindowSettings settings;
    check("setters return same object", &settings.setResourcePoolSize(resourceCount).setShaderPoolSize(shaderCount).setPipelinePoolSize(pipelineCount) == &settings);
    if (zero || negative) settings.setResourcePoolSize(negative ? -1 : 0).setShaderPoolSize(negative ? -2 : 0).setPipelinePoolSize(negative ? -3 : 0);
    settings.setSize(64, 64).setHighDpi(false).setSampleCount(1).setSwapInterval(0);
    return tcCoreTest::runDisplayTest<PoolApp>(settings, failures, [negative] {
        check("test completed", completed);
        int notices = 0, errors = 0;
        for (const auto& event : messages) {
            if (event.level == LogLevel::Notice && event.message.find("received a negative count; using default") != std::string::npos) ++notices;
            if (event.level == LogLevel::Error) ++errors;
        }
        check("one Notice per negative setting, none for zero/positive", notices == (negative ? 3 : 0));
        check("exactly one Error per pool", errors == 7);
    });
}
