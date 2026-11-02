// Own process: retaining a Window through runApp and through static destruction.
#if !defined(__linux__) || defined(__ANDROID__)
#error This own-process regression requires the returning Linux runner.
#endif
#include <TrussC.h>
#include <cstdio>
#include <cstring>
#include <array>
#include <barrier>
#include <thread>
using namespace tc;
#if defined(TC_LATE_MAC_DELETER)
#include "late-mac-deleter.h"
#endif
namespace {
std::shared_ptr<Window> makeRetainedWindow() {
    auto* window = new Window();
#if defined(TC_LATE_MAC_DELETER)
    return std::shared_ptr<Window>(window, internal::lateMacWindowDeleter);
#else
    return std::shared_ptr<Window>(window);
#endif
}
constinit std::atomic<bool> forbidLateCalls{false};
bool allowLateCalls = false;
void checkLateCall(const char* name) {
    if (forbidLateCalls.load(std::memory_order_acquire) && !allowLateCalls) {
        std::fprintf(stderr, "late-window: forbidden call after runner: %s\n", name);
        std::abort();
    }
}
struct FinalCheck {
    ~FinalCheck() {
        std::puts("late-window: static destruction completed");
    }
} finalCheck;
std::shared_ptr<Window> retained;
std::unique_ptr<Fbo> retainedFbo;
std::unique_ptr<Image> retainedImage;
std::unique_ptr<Light> retainedLight;
std::unique_ptr<Material> retainedMaterial;
std::unique_ptr<Environment> retainedEnvironment;
size_t initialFbos = 0, initialImages = 0, initialWindows = 0;
int restartFailures = 0;
constexpr size_t workerCount = 8, objectsPerWorker = 16;
struct RetainedObjects {
    std::shared_ptr<Window> window;
    std::unique_ptr<Fbo> fbo;
    std::unique_ptr<Image> image;
    std::unique_ptr<Light> light;
    std::unique_ptr<Material> material;
    std::unique_ptr<Environment> environment;
};
std::array<std::vector<RetainedObjects>, workerCount> concurrentObjects;
bool concurrentRestart = false;
void restartCheck(bool ok, const char* message) {
    if (!ok) {
        ++restartFailures;
        std::fprintf(stderr, "late-window: restart FAIL: %s\n", message);
    }
}
void checkLighting(internal::WindowContext& ctx) {
    restartCheck(ctx.activeLights.empty() && !ctx.currentMaterial && !ctx.currentEnvironment,
                 "destroyed lighting objects removed from context");
}
struct RestartFirst : App {
    void setup() override {
        initialFbos = internal::debugObjects(internal::DebugObjectKind::Fbo).size();
        initialImages = internal::debugObjects(internal::DebugObjectKind::Image).size();
        initialWindows = internal::openWindows().size();
        retained = makeRetainedWindow();
        retainedFbo = std::make_unique<Fbo>();
        retainedImage = std::make_unique<Image>();
        retainedFbo->setDebugName("retained through the first headless run");
        retainedImage->setDebugName("retained through the first headless run");
        retainedLight = std::make_unique<Light>();
        retainedMaterial = std::make_unique<Material>();
        retainedEnvironment = std::make_unique<Environment>();
        for (auto* ctx : {&internal::nonWindowContext(), &trussc::internal::WindowRequestAccess::context(*internal::resolveWindow()), &trussc::internal::WindowRequestAccess::context(*retained)}) {
            ctx->activeLights.push_back(retainedLight.get());
            ctx->currentMaterial = retainedMaterial.get();
            ctx->currentEnvironment = retainedEnvironment.get();
        }
        if (concurrentRestart) {
            for (auto& batch : concurrentObjects) {
                for (size_t i = 0; i < objectsPerWorker; ++i) {
                    batch.push_back({makeRetainedWindow(),
                        std::make_unique<Fbo>(), std::make_unique<Image>(), std::make_unique<Light>(),
                        std::make_unique<Material>(), std::make_unique<Environment>()});
                }
            }
            // Every light destructor scrubs the same vectors while peers erase
            // Windows. Keep one Window alive to inspect its scrubbed context.
            auto populate = [&](internal::WindowContext& ctx) {
                for (auto& batch : concurrentObjects) for (auto& objects : batch)
                    ctx.activeLights.push_back(objects.light.get());
                ctx.currentMaterial = concurrentObjects.back().back().material.get();
                ctx.currentEnvironment = concurrentObjects.front().front().environment.get();
            };
            populate(internal::nonWindowContext());
            populate(trussc::internal::WindowRequestAccess::context(*internal::resolveWindow()));
            populate(trussc::internal::WindowRequestAccess::context(*retained));
            for (auto& batch : concurrentObjects) for (auto& objects : batch) populate(trussc::internal::WindowRequestAccess::context(*objects.window));
        }
        requestExitApp();
    }
};
struct RestartSecond : App {
    void setup() override {
        restartCheck(internal::debugObjects(internal::DebugObjectKind::Fbo).size() == initialFbos,
                     "Fbo registry empty after restart");
        restartCheck(internal::debugObjects(internal::DebugObjectKind::Image).size() == initialImages,
                     "Image registry empty after restart");
        restartCheck(internal::openWindows().size() == initialWindows, "window registry empty after restart");
        checkLighting(internal::nonWindowContext());
        checkLighting(trussc::internal::WindowRequestAccess::context(*internal::resolveWindow()));
        requestExitApp();
    }
};
int secondaryDraws = 0;
int mainExits = 0;
struct Secondary : App {
    Font font;
    Fbo fbo;
    ~Secondary() override {
        if (internal::frameworkShutdown() &&
            (getWindow() || !internal::debugObjects(internal::DebugObjectKind::Fbo).empty()))
            std::abort();
    }
    void setup() override {
        if (!font.load(TC_FONT_SANS, 16)) std::abort();
        fbo.allocate(16, 16);
        if (!fbo.isAllocated()) std::abort();
    }
    void draw() override {
        ++secondaryDraws; clear(0.1f); drawRect(2, 2, 24, 24);
        fbo.begin(); clear(0.2f); fbo.end(); fbo.draw(0, 0);
        font.drawString("late", 0, 20);
    }
};
struct Main : App {
    int turns = 0;
    void setup() override {
        WindowSettings settings; settings.width = settings.height = 64;
        retained = createWindow(settings);
        if (!retained) std::abort();
        retained->setApp<Secondary>();
    }
    void update() override { if (++turns == 12) requestExitApp(); }
    void exit() override { ++mainExits; }
};
struct Headless : App {
    void setup() override {
        // Headless has no secondary native windows. Retain an unbound Window
        // to exercise the same destructor and non-window shutdown boundary.
        retained = makeRetainedWindow();
        requestExitApp();
    }
};
void release() {
    retained.reset();
    // Independently owned bundles must also be inert after sgl shutdown.
    internal::RenderTargetResources resources;
    resources.context.id = 0x10002; // a non-default context from the shut-down device
    resources.release();
    std::puts("late-window: release after runner completed");
}
}
// Linker interposition observes real production callers after runApp returns.
// Mutants allow the original calls so their failure must come from ASan.
extern "C" Logger& realGetLogger() asm("__real__ZN6trussc9getLoggerEv");
extern "C" Logger& wrapGetLogger() asm("__wrap__ZN6trussc9getLoggerEv");
extern "C" Logger& wrapGetLogger() { checkLateCall("getLogger"); return realGetLogger(); }
extern "C" internal::RenderContext& realDefaultContext() asm("__real__ZN6trussc8internal17getDefaultContextEv");
extern "C" internal::RenderContext& wrapDefaultContext() asm("__wrap__ZN6trussc8internal17getDefaultContextEv");
extern "C" internal::RenderContext& wrapDefaultContext() { checkLateCall("getDefaultContext"); return realDefaultContext(); }
extern "C" internal::WindowContext& realCurrentWindowContext() asm("__real__ZN6trussc8internal20currentWindowContextEv");
extern "C" internal::WindowContext& wrapCurrentWindowContext() asm("__wrap__ZN6trussc8internal20currentWindowContextEv");
extern "C" internal::WindowContext& wrapCurrentWindowContext() { checkLateCall("currentWindowContext"); return realCurrentWindowContext(); }
extern "C" bool __real_sg_isvalid(void);
extern "C" bool __wrap_sg_isvalid(void) { checkLateCall("sg_isvalid"); return __real_sg_isvalid(); }
extern "C" sgl_context __real_sgl_default_context(void);
extern "C" sgl_context __wrap_sgl_default_context(void) { checkLateCall("sgl_default_context"); return __real_sgl_default_context(); }
extern "C" void __real_sg_destroy_buffer(sg_buffer);
extern "C" void __wrap_sg_destroy_buffer(sg_buffer value) { checkLateCall("sg_destroy_buffer"); __real_sg_destroy_buffer(value); }
extern "C" void __real_sg_destroy_image(sg_image);
extern "C" void __wrap_sg_destroy_image(sg_image value) { checkLateCall("sg_destroy_image"); __real_sg_destroy_image(value); }
extern "C" void __real_sg_destroy_view(sg_view);
extern "C" void __wrap_sg_destroy_view(sg_view value) { checkLateCall("sg_destroy_view"); __real_sg_destroy_view(value); }
extern "C" void __real_sg_destroy_sampler(sg_sampler);
extern "C" void __wrap_sg_destroy_sampler(sg_sampler value) { checkLateCall("sg_destroy_sampler"); __real_sg_destroy_sampler(value); }
extern "C" void __real_sg_destroy_pipeline(sg_pipeline);
extern "C" void __wrap_sg_destroy_pipeline(sg_pipeline value) { checkLateCall("sg_destroy_pipeline"); __real_sg_destroy_pipeline(value); }
extern "C" void __real_sg_destroy_shader(sg_shader);
extern "C" void __wrap_sg_destroy_shader(sg_shader value) { checkLateCall("sg_destroy_shader"); __real_sg_destroy_shader(value); }
extern "C" void __real_sgl_destroy_pipeline(sgl_pipeline);
extern "C" void __wrap_sgl_destroy_pipeline(sgl_pipeline value) { checkLateCall("sgl_destroy_pipeline"); __real_sgl_destroy_pipeline(value); }
extern "C" void __real_sgl_destroy_context(sgl_context);
extern "C" void __wrap_sgl_destroy_context(sgl_context value) { checkLateCall("sgl_destroy_context"); __real_sgl_destroy_context(value); }
extern "C" void __real_sapp_window_request_close(sapp_window);
extern "C" void __wrap_sapp_window_request_close(sapp_window value) { checkLateCall("sapp_window_request_close"); __real_sapp_window_request_close(value); }
extern "C" void __real_sapp_destroy_window(sapp_window);
extern "C" void __wrap_sapp_destroy_window(sapp_window value) { checkLateCall("sapp_destroy_window"); __real_sapp_destroy_window(value); }

int main(int argc, char** argv) {
    std::fputs("late-window: entered main\n", stderr);
    const char* mode = argc > 1 ? argv[1] : "static";
    const bool scrubOnly = std::strcmp(mode, "concurrent-scrubs") == 0;
    concurrentRestart = std::strcmp(mode, "concurrent") == 0 || scrubOnly;
    if (std::strcmp(mode, "restart") == 0 || concurrentRestart) {
        if (runHeadlessApp<RestartFirst>()) return 2;
        forbidLateCalls.store(true, std::memory_order_release);
        restartCheck(internal::frameworkShutdown(), "run 1 stopped before destruction");
        if (concurrentRestart) {
            std::barrier start(static_cast<ptrdiff_t>(workerCount));
            std::array<std::thread, workerCount> workers;
            for (size_t i = 0; i < workerCount; ++i) workers[i] = std::thread([&, i] {
                start.arrive_and_wait();
                for (auto& objects : concurrentObjects[i]) {
                    // Alternate the order so scrub/erase as well as erase/erase
                    // overlap. No context or GPU/native access on workers.
                    if (!scrubOnly && i % 2) objects.window.reset();
                    objects.light.reset();
                    objects.material.reset(); objects.environment.reset();
                    objects.fbo.reset(); objects.image.reset();
                    if (!scrubOnly) objects.window.reset();
                }
            });
            for (auto& worker : workers) worker.join();
            // The focused scrub-lock mutation keeps contexts alive until join
            // so its race report can complete instead of crashing on a freed Window.
            for (auto& batch : concurrentObjects) for (auto& objects : batch) objects.window.reset();
            // Enumeration remains main-thread-only; stopped queries are empty.
            restartCheck(internal::openWindows().empty(), "stopped window enumeration");
            restartCheck(internal::debugObjects(internal::DebugObjectKind::Fbo).empty(), "stopped Fbo enumeration");
            restartCheck(internal::debugObjects(internal::DebugObjectKind::Image).empty(), "stopped Image enumeration");
        }
        retainedLight.reset(); retainedMaterial.reset(); retainedEnvironment.reset();
        checkLighting(trussc::internal::WindowRequestAccess::context(*retained));
        retainedFbo.reset(); retainedImage.reset(); retained.reset();
        forbidLateCalls.store(false, std::memory_order_release);
        if (runHeadlessApp<RestartSecond>()) return 2;
        std::printf("late-window: restart completed, %d failures\n", restartFailures);
        return restartFailures ? 1 : 0;
    }
    int result;
    if (std::strcmp(mode, "headless") == 0) result = runHeadlessApp<Headless>();
    else {
        WindowSettings settings; settings.width = settings.height = 64;
        // runapp-verdict: linux-returning-lifetime -- this Linux-only probe must release a handle after runApp returns
        result = runApp<Main>(settings);
    }
    const char* mutation = std::getenv("TC_LATE_WINDOW_MUTATION");
    allowLateCalls = mutation && std::strcmp(mutation, "baseline") != 0;
    forbidLateCalls.store(true, std::memory_order_release);
    if (result || !retained || (std::strcmp(mode, "headless") != 0 &&
        (secondaryDraws == 0 || mainExits != 1 || !retained->isOpen()))) return 1;
    std::printf("late-window: stopped=%d\n", internal::frameworkShutdown());
    if (std::strcmp(mode, "static") == 0) return 0;
    if (std::strcmp(mode, "thread") == 0) { std::thread worker(release); worker.join(); }
    else release();
    return 0;
}
