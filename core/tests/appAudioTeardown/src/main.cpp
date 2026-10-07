// #698: cleanup may free App audio state on every headless teardown path.
// The null backend drives real callbacks. Handshakes, not elapsed time,
// hold a callback in flight until detachment starts and keep audio running
// across cleanup. Run under ASan to check the buffer lifetime too.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

template<class Predicate>
void waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::fprintf(stderr, "FAIL: audio handshake did not complete\n");
            std::_Exit(2);
        }
        std::this_thread::yield();
    }
}

struct Probe {
    std::atomic<int> calls{0}, lateCalls{0}, passes{0};
    std::atomic<bool> arm{false}, entered{false}, release{false};
    std::atomic<bool> finished{false}, cleanupStarted{false};
    bool exited = false;
    bool hooksInExit = false;
    bool detachedInCleanup = false;
    bool finishedInCleanup = false;
    bool exitInCleanup = false;
    int cleanups = 0;
};

struct BufferApp : App {
    Probe& probe;
    std::unique_ptr<float[]> samples;
    explicit BufferApp(Probe& p) : probe(p) {}
    void setup() override { samples = std::make_unique<float[]>(256); }
    void audioOut(AudioOutBuffer& buffer) override {
        ++probe.calls;
        if (probe.cleanupStarted) ++probe.lateCalls;
        float* data = samples.get();
        if (probe.arm.exchange(false)) {
            probe.entered = true;
            // Test-only gate: another worker releases this callback when
            // the main thread disconnects. No main-thread lock is held.
            waitFor([&] { return probe.release.load(); });
        }
        if (data) buffer.data[0] += data[0];
        if (probe.entered) probe.finished = true;
    }
    void exit() override {
        probe.exited = true;
        auto& engine = AudioEngine::getInstance();
        probe.hooksInExit = engine.audioOut.listenerCount() == 2 &&
                            engine.audioIn.listenerCount() == 1;
    }
    void cleanup() override {
        probe.cleanupStarted = true;
        ++probe.cleanups;
        probe.exitInCleanup = probe.exited;
        probe.finishedInCleanup = probe.finished;
        auto& engine = AudioEngine::getInstance();
        probe.detachedInCleanup = engine.audioOut.listenerCount() == 1 &&
                                  engine.audioIn.listenerCount() == 0;
        samples.reset();
        const int start = probe.passes;
        waitFor([&] { return probe.passes >= start + 3; });
    }
};

void armCallback(Probe& p) {
    p.arm = true;
    waitFor([&] { return p.entered.load(); });
}

// A native-free window, like windowAppSwap. landClose is the platform's
// endApp boundary after close() has recorded its request.
uint64_t nativeStandIn = 0;
struct OpenWindow : Window {
    OpenWindow() { native_ = &nativeStandIn; }
    void landClose() {
        native_ = nullptr;
        closeRequested_ = false;
        internal::WindowRequestAccess::endApp(*this);
    }
    ~OpenWindow() { native_ = nullptr; }
};

void tick(Window& window) {
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &window.context();
    {
        internal::WindowDispatchScope scope(window);
        window.tickTree();
    }
    internal::currentWindowCtx() = previous;
}

Probe* headlessProbe = nullptr;
struct HeadlessApp : BufferApp {
    HeadlessApp() : BufferApp(*headlessProbe) {}
    void update() override {
        armCallback(probe);
        requestExit();
    }
};

enum class TeardownPath { Swap, Remove, Close, Child, Headless };
void runPath(TeardownPath path, const char* name) {
    std::printf("Teardown path: %s\n", name);
    Probe p;
    auto& engine = AudioEngine::getInstance();
    auto monitor = engine.audioOut.listen([&](AudioOutBuffer&) { ++p.passes; });
    bool cleanupBeforeRelease = false;
    std::thread release([&] {
        waitFor([&] { return p.entered.load(); });
        waitFor([&] {
            return engine.audioOut.listenerCount() == 1 || p.cleanupStarted.load();
        });
        cleanupBeforeRelease = p.cleanupStarted;
        p.release = true;
    });
    if (path == TeardownPath::Headless) {
        headlessProbe = &p;
        runHeadlessApp<HeadlessApp>();
        headlessProbe = nullptr;
    } else {
        auto app = std::make_shared<BufferApp>(p);
        OpenWindow window;
        if (path == TeardownPath::Child) {
            auto parent = std::make_shared<Node>();
            window.setApp(std::make_shared<App>());
            tick(window);
            // Keep only the child App subscribed, to use the same counts.
            internal::detachAppAudio(*window.getApp());
            window.getApp()->addChild(parent);
            parent->addChild(app);
            tick(window);
            armCallback(p);
            app->destroy();
            tick(window);
        } else {
            window.setApp(app);
            tick(window);
            armCallback(p);
            if (path == TeardownPath::Close) {
                window.close();
                window.landClose();
            } else {
                window.setApp(path == TeardownPath::Swap ? std::make_shared<App>() : nullptr);
                internal::applyPendingAppForTests(window);
            }
        }
        window.landClose();
    }
    release.join();
    check("callback ran before cleanup", p.calls > 0);
    check("cleanup waited for the in-flight callback", !cleanupBeforeRelease && p.finishedInCleanup);
    check("both hooks detached before cleanup", p.detachedInCleanup);
    check("no audioOut after cleanup started", p.lateCalls == 0);
    check("cleanup ran once", p.cleanups == 1);
    if (path != TeardownPath::Child) {
        check("exit precedes detachment and cleanup", p.hooksInExit && p.exitInCleanup);
    }
    monitor.disconnect();
    engine.waitForAudioCallbacks();
}
} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::setNullAudioBackendForTests(true);
    AudioSettings settings;
    settings.sampleRate = 48000;
    settings.channels = 2;
    settings.bufferSize = 256;
    auto& engine = AudioEngine::getInstance();
    if (!engine.init(settings)) return 1;
    runPath(TeardownPath::Swap, "setApp swap");
    runPath(TeardownPath::Remove, "setApp nullptr");
    runPath(TeardownPath::Close, "close/endApp");
    runPath(TeardownPath::Child, "child App destroy");
    runPath(TeardownPath::Headless, "headless exit");
    engine.shutdown();
    return failures ? 1 : 0;
}
