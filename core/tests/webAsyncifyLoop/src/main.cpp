#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstring>

namespace {
#ifdef __EMSCRIPTEN__
int failures = 0;
bool setupSleep = true;
bool updateSleep = true;
int setupEntries = 0, setupCompletions = 0;
int frameEntries = 0, frameCompletions = 0, draws = 0;
int activeDepth = 0, reentries = 0, sleeps = 0, resumes = 0;
int updatesAtResume = 0, drawsAtResume = 0;
bool progressPending = false, completed = false;
const char* stage = "startup";

void publish() {
    EM_ASM({
        window.tcAsyncifyLoop = ({
            stage: UTF8ToString($0), setupEntries: $1, setupCompletions: $2,
            frameEntries: $3, frameCompletions: $4, draws: $5,
            activeDepth: $6, reentries: $7, sleeps: $8, resumes: $9
        });
    }, stage, setupEntries, setupCompletions, frameEntries, frameCompletions,
       draws, activeDepth, reentries, sleeps, resumes);
}

void check(const char* label, bool ok) {
    std::printf("webAsyncifyLoop %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Depth spans the real suspension, including all repeated sleeps in a callback.
struct Callback {
    Callback() {
        if (activeDepth) ++reentries;
        ++activeDepth;
    }
    ~Callback() {
        --activeDepth;
        publish();
    }
};

void sleepOnce(const char* label) {
    stage = label;
    ++sleeps;
    publish();
    emscripten_sleep(10);
    ++resumes;
    updatesAtResume = frameEntries;
    drawsAtResume = draws;
    progressPending = true;
    stage = "resumed";
    publish();
}

class LoopApp : public tc::App {
    void setup() override {
        Callback callback;
        ++setupEntries;
        if (setupSleep) sleepOnce("setup sleep");
        ++setupCompletions;
        stage = "setup complete";
    }

    void update() override {
        Callback callback;
        ++frameEntries;
        if (progressPending && draws > drawsAtResume) {
            check("updates and draws continue after resume", frameEntries > updatesAtResume);
            progressPending = false;
        }
        // First prove setup resumed and drew; later suspend ordinary frames.
        if (updateSleep && frameEntries == 3) sleepOnce("update sleep");
        if (updateSleep && frameEntries == 6) {
            for (int i = 0; i < 3; ++i) sleepOnce("repeated update sleep");
        }
        ++frameCompletions;
        stage = "update complete";
    }

    void draw() override {
        Callback callback;
        ++draws;
        tc::clear(0.1f, 0.2f, float(draws % 4) / 4.0f);
        stage = "draw complete";
        if (frameCompletions >= 10 && draws >= 10) {
            completed = true;
            tc::exitApp();
        }
    }
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
    if (argc < 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::puts("SKIP: webAsyncifyLoop needs the browser runner and --gpu-check");
        return 0;
    }
    const char* mode = argc > 2 ? argv[2] : "all";
    setupSleep = std::strcmp(mode, "all") == 0;
    updateSleep = std::strcmp(mode, "control") != 0;
    if (!setupSleep && std::strcmp(mode, "update") != 0 && updateSleep) return 2;
    tc::WindowSettings settings;
    settings.setSize(160, 120).setHighDpi(false).setSampleCount(1);
    return tcCoreTest::runDisplayTest<LoopApp>(settings, failures, [] {
        stage = "cleanup";
        publish();
        check("test completed", completed);
        check("setup completed once", setupEntries == 1 && setupCompletions == 1);
        check("frames completed", frameEntries >= 10 && frameEntries == frameCompletions);
        check("draws continued", draws >= 10 && !progressPending);
        check("no callback entered during suspension", activeDepth == 0 && reentries == 0);
        check("all real sleeps resumed", sleeps == (setupSleep ? 1 : 0) + (updateSleep ? 4 : 0) &&
              resumes == sleeps);
    });
#else
    (void)argc;
    (void)argv;
    std::puts("SKIP: webAsyncifyLoop requires a Web build and browser");
    return 0;
#endif
}
