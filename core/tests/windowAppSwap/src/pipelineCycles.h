#pragma once

// Optional real-window half of windowAppSwap. Uses the same entry/runner as
// the headless checks; Linux needs X11 (e.g. Xvfb), Windows/macOS a desktop.
namespace { namespace pipelineCycles {
using namespace tc;
inline std::string mode;
inline int cycles = 100;
inline int failures = 0;

struct Probe {
    int updates = 0;
    int exits = 0;
    int cleanups = 0;
};

struct Secondary : App {
    Probe& probe;
    explicit Secondary(Probe& p) : probe(p) {}
    void update() override {
        if (++probe.updates == 3 && mode == "self") getWindow()->close();
    }
    void draw() override {
        tc::drawRect(10, 10, 30, 30);
        // Exercise more than one cache entry, including repeated lookups.
        setBlendMode(BlendMode::Add);
        tc::drawRect(20, 20, 30, 30);
        setBlendMode(BlendMode::Alpha);
    }
    void exit() override { ++probe.exits; }
    void cleanup() override { ++probe.cleanups; }
};

struct Main : App {
    Probe probe;  // outlives the Window's App
    std::shared_ptr<Window> window;
    uint32_t baseline = 0;
    int frames = 0, completed = 0, closedFrames = 0;
#ifndef NDEBUG
    size_t ownerBaseline = 0;
#endif

    void check(bool ok, const char* what) {
        if (!ok) {
            ++failures;
            printf("pipeline-cycles %s cycle=%d FAIL: %s\n", mode.c_str(), completed + 1, what);
        }
    }
    void draw() override { tc::drawRect(10, 10, 30, 30); }
    void update() override {
        // Warm the main window's lazy resources before taking the baseline.
        if (++frames < 4) return;
        if (frames == 4) {
            baseline = sg_query_stats().total.pipelines.alive;
#ifndef NDEBUG
            ownerBaseline = internal::pipelineOwnerCtx().size();
#endif
            printf("pipeline-cycles %s baseline=%u pool=%d\n", mode.c_str(), baseline,
                   sg_query_desc().pipeline_pool_size);
            fflush(stdout);
        }
        if (window) {
            if (window->isOpen()) {
                if (mode == "main" && probe.updates >= 3) window->close();
                return;
            }
            // Keep the closed Window alive across two more main frames: any
            // late backend tick must neither recreate its context nor leak.
            const uint32_t live = sg_query_stats().total.pipelines.alive;
            check(live == baseline, "live sg pipelines did not return to baseline");
            check(window->context().swapchainTarget.context.id == SG_INVALID_ID,
                  "closed window recreated its sgl context");
            check(window->context().swapchainTarget.cache.empty(), "closed window retained pipelines");
#ifndef NDEBUG
            check(internal::pipelineOwnerCtx().size() == ownerBaseline,
                  "pipeline ownership bookkeeping did not return to baseline");
#endif
            if (++closedFrames < 3) return;
            if (mode != "early") {
                check(probe.updates >= 3, "secondary never reached its third update");
                check(probe.exits == 1 && probe.cleanups == 1, "App teardown did not run exactly once");
            }
            printf("pipeline-cycles %s cycle=%d live=%u baseline=%u updates=%d\n",
                   mode.c_str(), ++completed, live, baseline, probe.updates);
            fflush(stdout);
            window.reset();
            if (completed == cycles) { exitApp(); return; }
        }
        probe = {};
        closedFrames = 0;
        WindowSettings settings;
        settings.setSize(160, 120);
        settings.title = "pipeline-cycle";
        window = createWindow(settings);
        check(bool(window), "createWindow failed");
        if (!window) { exitApp(); return; }
        window->setApp(std::make_shared<Secondary>(probe));
        // Exercise a request made before the first tick has allocated anything.
        if (mode == "early") window->close();
    }
    void cleanup() override {
        check(completed == cycles, "not all cycles completed");
        window.reset();
    }
};

inline int run(const char* closeMode, int count) {
    mode = closeMode;
    cycles = count;
    failures = 0;
    if ((mode != "self" && mode != "main" && mode != "early") || cycles <= 0) return 2;
    WindowSettings settings;
    settings.setSize(320, 240);
    settings.title = "windowAppSwap pipeline cycles";
    runApp<Main>(settings);
    printf("pipeline-cycles %s: %s (%d failures)\n", mode.c_str(), failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
}} // anonymous namespace / pipelineCycles
