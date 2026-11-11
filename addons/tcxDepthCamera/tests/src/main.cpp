// Headless depth-camera lifecycle and frame-publication regressions.
// Also the device lifecycle rules of docs/architecture/DEVICE_LIFECYCLE.md (#415): setup()
// reopens, isConnected(), close() / the destructor never fire onDisconnect, and
// a backend's reportLost() fires it once, on the main thread for Deliver::Main.
// Built by examples/build_all.py --addon-tests-only.
#include <tcxDepthCamera.h>
#include <tcxDepthRecord.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {
using namespace tcx::depthcamera;
using namespace tcx::depthrecord;

int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}

struct TestState {
    std::mutex mutex;
    std::condition_variable changed;
    int allowed = 0;
    int entered = 0;
    int opens = 0;
    int closes = 0;
    bool open = false;
    bool canOpen = true;
    std::atomic<int> active{0};
    std::atomic<bool> capturedWhileClosed{false};
    bool closedWhileActive = false;
    bool backendGoneWhileActive = false;
    bool draining = false;
    void drain() {
        std::lock_guard lock(mutex);
        draining = true;
        changed.notify_all();
    }

    // The next capture entering proves that the preceding capture's swap has
    // completed. The timeout only bounds a deadlock; elapsed time is not tested.
    bool waitEntered(int n) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(10), [&] { return entered >= n; });
    }
    void allow(int n) {
        std::lock_guard lock(mutex);
        allowed = n;
        changed.notify_all();
    }
};

class CountingCamera : public DepthCamera {
public:
    explicit CountingCamera(TestState& state) : state_(state), witness_{state} {}

protected:
    struct CountingDevice : Device {
        TestState& state;
        bool threaded;
        int call = 0;
        CountingDevice(TestState& value, bool useThread) : state(value), threaded(useThread) {
            state.open = true;
            ++state.opens;
        }
        ~CountingDevice() override {
            if (state.active != 0) state.closedWhileActive = true;
            state.open = false;
            ++state.closes;
        }
        StreamFreshness captureInto(DepthFrame& dst) override {
            ++state.active;
            if (!state.open) state.capturedWhileClosed = true;
            ++call;
            if (threaded) {
                std::unique_lock lock(state.mutex);
                ++state.entered;
                state.changed.notify_all();
                state.changed.wait(lock, [&] { return state.allowed >= state.entered || state.draining; });
            }
            StreamFreshness fresh;
            if ((call % 2) != 0) {
                dst.w = dst.h = 1;
                dst.depth.assign(1, static_cast<uint16_t>(call));
                dst.timestamp = call;
                fresh.depth = true;
            }
            --state.active;
            return fresh;
        }
    };
    std::shared_ptr<Device> openDevice() override {
        if (!state_.canOpen) return {};
        state_.draining = false;
        return std::make_shared<CountingDevice>(state_, isThreaded());
    }

private:
    struct Witness {
        TestState& state;
        ~Witness() {
            state.backendGoneWhileActive = state.active != 0;
            state.drain();
        }
    };
    TestState& state_;
    Witness witness_;
};

// The user callback deliberately closes/deletes the handle from capture.
// Everything used after it returns belongs to the Device and the run.
void ownThreadTeardown() {
    struct Probe {
        std::mutex mutex;
        std::condition_variable changed;
        bool start = false;
        bool released = false;
        bool wroteAfterCallback = false;
        std::function<void()> action;
    };
    class CallbackCamera : public DepthCamera {
    public:
        explicit CallbackCamera(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    protected:
        struct CallbackDevice : Device {
            std::shared_ptr<Probe> probe;
            explicit CallbackDevice(std::shared_ptr<Probe> value) : probe(std::move(value)) {}
            ~CallbackDevice() override {
                std::lock_guard lock(probe->mutex);
                probe->released = true;
                probe->changed.notify_all();
            }
            StreamFreshness captureInto(DepthFrame& dst) override {
                {
                    std::unique_lock lock(probe->mutex);
                    probe->changed.wait(lock, [&] { return probe->start; });
                }
                probe->action();
                dst.w = dst.h = 1;
                dst.depth.assign(1, 123);
                probe->wroteAfterCallback = true;
                StreamFreshness fresh;
                fresh.depth = true;
                return fresh;
            }
        };
        std::shared_ptr<Device> openDevice() override { return std::make_shared<CallbackDevice>(probe_); }
    private:
        std::shared_ptr<Probe> probe_;
    };
    for (bool destroy : {false, true}) {
        auto probe = std::make_shared<Probe>();
        auto camera = std::make_unique<CallbackCamera>(probe);
        camera->setThreaded(true);
        probe->action = [&] { if (destroy) camera.reset(); else camera->close(); };
        check("callback camera setup", camera->setup());
        std::unique_lock lock(probe->mutex);
        probe->start = true;
        probe->changed.notify_all();
        const bool ended = probe->changed.wait_for(lock, std::chrono::seconds(10), [&] { return probe->released; });
        check(destroy ? "destroy from capture retains Device and frame until worker ends"
                      : "close from capture retains Device and frame until worker ends",
              ended && probe->wroteAfterCallback);
    }
}

void lifecycle() {
    TestState state;
    {
        CountingCamera camera(state);
        camera.setThreaded(true);
        check("first setup", camera.setup());
        check("first worker entered capture", state.waitEntered(1));
        state.drain();
        check("setup while capture is active", camera.setup());
        check("setup closed the old session before reopening", state.opens == 2 && state.closes == 1);
        check("reopened worker entered capture", state.waitEntered(2));
        state.drain();
        state.canOpen = false;
        check("failed setup closes the old session", !camera.setup() && !state.open && state.active == 0);
        state.canOpen = true;
        camera.close();
        camera.close();
        check("repeated close balances successful opens", state.opens == state.closes);
        check("capture never overlaps a closed device", !state.capturedWhileClosed && !state.closedWhileActive);
        check("setup after repeated close", camera.setup());
        check("worker active before implicit teardown", state.waitEntered(3));
        // No explicit close(): backend members die while the Device is capturing.
    }
    check("destructor closes every session", state.opens == 3 && state.closes == 3);
    check("capture safely outlives backend members", state.backendGoneWhileActive);
}

void rollback(bool threaded) {
    TestState state;
    CountingCamera camera(state);
    camera.setThreaded(threaded);
    check(threaded ? "threaded rollback setup" : "inline rollback setup", camera.setup());
    bool correct = true;
    for (int call = 1; call <= 200; ++call) {
        if (threaded) {
            state.allow(call);
            if (!state.waitEntered(call + 1)) { correct = false; break; }
        }
        camera.update();
        const int expected = (call % 2) != 0 ? call : call - 1;
        const auto& frame = camera.currentFrame();
        correct &= frame.depth.size() == 1 && frame.depth[0] == expected;
        correct &= frame.timestamp == expected;
        correct &= camera.isFrameNew() == ((call % 2) != 0);
    }
    check(threaded ? "threaded empty captures preserve frame and freshness"
                   : "inline empty captures preserve frame and freshness", correct);
}

void pendingFrame() {
    TestState state;
    CountingCamera camera(state);
    camera.setThreaded(true);
    check("pending-frame setup", camera.setup());
    state.allow(6); // Three fresh frames and three empty captures before update().
    const bool captured = state.waitEntered(7);
    camera.update();
    const auto& frame = camera.currentFrame();
    check("empty captures preserve the newest unpublished frame",
          captured && frame.depth.size() == 1 && frame.depth[0] == 5 && camera.isFrameNew());
    camera.update();
    check("unpublished freshness is consumed once", !camera.isFrameNew());
}

template<class Camera>
bool waitForFrame(Camera& camera) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        camera.update();
        if (camera.isFrameNew()) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

// Gate actual playback to verify EOF and looping without racing the producer.
class GatedPlayback : public PlaybackDepthCamera {
public:
    GatedPlayback(const std::string& path, TestState& state)
        : PlaybackDepthCamera(path), state_(state) {}
    ~GatedPlayback() override { state_.drain(); }
protected:
    struct GatedDevice : PlaybackDevice {
        TestState& state;
        bool threaded;
        GatedDevice(TestState& value, bool useThread) : state(value), threaded(useThread) {}
        StreamFreshness captureInto(DepthFrame& dst) override {
            if (threaded) {
                std::unique_lock lock(state.mutex);
                ++state.entered;
                state.changed.notify_all();
                state.changed.wait(lock, [&] { return state.allowed >= state.entered || state.draining; });
            }
            return PlaybackDevice::captureInto(dst);
        }
    };
    std::shared_ptr<PlaybackDevice> makePlaybackDevice() override {
        return std::make_shared<GatedDevice>(state_, isThreaded());
    }
private:
    TestState& state_;
};

void playback() {
    // Keep the fixture inside the test working directory and remove it at exit.
    const auto path = std::filesystem::absolute("depthcamera-lifecycle.tcdc");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
    } cleanup{path};
    {
        TestState state;
        CountingCamera source(state);
        source.setup();
        DepthRecorder recorder;
        check("create playback fixture", recorder.start(path.string(), REC_DEPTH));
        for (int i = 0; i < 5; ++i) {
            source.update();
            if (source.isFrameNew()) recorder.record(source);
        }
        recorder.stop();
    }
    for (bool threaded : {false, true}) {
        TestState state;
        GatedPlayback camera(path.string(), state);
        camera.setThreaded(threaded);
        camera.setLoop(false);
        check("playback setup", camera.setup() && camera.getFrameCount() == 3);
        bool eof = true;
        for (int call = 1; call <= 20; ++call) {
            if (threaded) {
                state.allow(call);
                if (!state.waitEntered(call + 1)) { eof = false; break; }
            }
            camera.update();
            const int expected = call <= 3 ? call * 2 - 1 : 5;
            const auto& frame = camera.currentFrame();
            eof &= frame.depth.size() == 1 && frame.depth[0] == expected;
            eof &= camera.isFrameNew() == (call <= 3);
        }
        check(threaded ? "threaded playback EOF preserves last frame"
                       : "inline playback EOF preserves last frame", eof);
        camera.setLoop(true);
        bool loop = true;
        for (int call = 21; call <= 26; ++call) {
            if (threaded) {
                state.allow(call);
                if (!state.waitEntered(call + 1)) { loop = false; break; }
            }
            camera.update();
            const int expected = ((call - 21) % 3) * 2 + 1;
            const auto& frame = camera.currentFrame();
            loop &= frame.depth.size() == 1 && frame.depth[0] == expected && camera.isFrameNew();
        }
        check(threaded ? "threaded playback loops after EOF" : "inline playback loops after EOF", loop);
    }
    bool teardown = true;
    for (int i = 0; i < 20; ++i) {
        // Exercise the shipped destructor itself, with no subclass or close().
        auto camera = std::make_unique<PlaybackDepthCamera>(path.string());
        camera->setThreaded(true);
        camera->setLoop(true);
        teardown &= camera->setup() && waitForFrame(*camera);
        camera.reset();
    }
    check("threaded PlaybackDepthCamera teardown without close", teardown);
    {
        SyntheticDepthCamera camera(8, 8);
        camera.enableDepth();
        camera.setThreaded(true);
        check("threaded SyntheticDepthCamera teardown without close", camera.setup() && waitForFrame(camera));
    }
}

// A backend that reports a loss: the test calls lose() where a real backend
// calls reportLost() from its SDK callback or from captureInto().
class LossCamera : public SyntheticDepthCamera {
public:
    LossCamera() : SyntheticDepthCamera(4, 4) { enableDepth(); }
    ~LossCamera() override { close(); }  // the worker calls lose(): join it while this still exists
    void lose(const std::string& reason) { reportLost(reason); }
    // Report the loss from inside captureInto(), on the capture thread
    void loseOnCapture(const std::string& reason) {
        reasonOnCapture_ = reason;
        loseOnCapture_ = true;
    }
    int opens = 0;
    std::atomic<int> captures{0};
protected:
    struct LossDevice : SyntheticDevice {
        LossCamera& owner;
        LossDevice(LossCamera& o, bool threaded)
            : SyntheticDevice(4, 4, true, false, threaded), owner(o) {}
        StreamFreshness captureInto(DepthFrame& dst) override {
            ++owner.captures;
            if (owner.loseOnCapture_.exchange(false)) owner.reportLost(owner.reasonOnCapture_);
            return SyntheticDevice::captureInto(dst);
        }
    };
    std::shared_ptr<Device> openDevice() override {
        if (!canOpen) return {};
        ++opens;
        return std::make_shared<LossDevice>(*this, isThreaded());
    }
public:
    bool canOpen = true;
private:
    std::atomic<bool> loseOnCapture_{false};
    std::string reasonOnCapture_;
};

// Records onDisconnect notifications of one camera
struct LossRecorder {
    int count = 0;
    std::string reason;
    bool connectedInside = true;
    bool onMain = false;
    EventListener sub;
    void attach(DepthCamera& cam, Deliver deliver = Deliver::Inline) {
        sub = cam.onDisconnect.listen([this, &cam](DeviceDisconnectEventArgs& e) {
            ++count;
            reason = e.reason;
            connectedInside = cam.isConnected();
            onMain = isMainThread();
        }, deliver);
    }
};

int g_setupErrors = 0;

void deviceLifecycle() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("setup failed") != std::string::npos) ++g_setupErrors;
    });
    {
        LossCamera cam;
        LossRecorder rec;
        rec.attach(cam);
        check("lifecycle 1. not connected before setup()", !cam.isConnected());
        cam.canOpen = false;
        const int errors = g_setupErrors;
        check("lifecycle 2. a failed setup() returns false, stays disconnected, logs an error",
              !cam.setup() && !cam.isConnected() && g_setupErrors == errors + 1);
        cam.canOpen = true;
        check("lifecycle 3. setup() connects", cam.setup() && cam.isConnected());
        cam.close();
        check("lifecycle 4. close() disconnects", !cam.isConnected());
        check("lifecycle 5. setup() with no arguments reopens", cam.setup() && cam.isConnected() && cam.opens == 2);
        check("lifecycle 6. setup() on an open camera reopens it", cam.setup() && cam.isConnected() && cam.opens == 3);
        cam.close();
        cam.lose("not open");
        check("lifecycle 7. nothing fired from setup(), close() or a report on a closed camera", rec.count == 0);
    }
    {
        // A reported loss: once, with isConnected() already false
        LossCamera cam;
        LossRecorder rec;
        rec.attach(cam);
        cam.setup();
        cam.lose("usb removed");
        check("lifecycle 8. reportLost() fires onDisconnect once with the reason",
              rec.count == 1 && rec.reason == "usb removed");
        check("lifecycle 9. isConnected() is already false in the listener",
              !rec.connectedInside && !cam.isConnected());
        cam.lose("again");
        cam.close();
        check("lifecycle 10. a second report and close() add nothing", rec.count == 1);
        check("lifecycle 11. setup() reconnects after a loss", cam.setup() && cam.isConnected());
        cam.lose("second loss");
        check("lifecycle 12. the next session reports its own loss", rec.count == 2 && rec.reason == "second loss");
    }
    {
        // Inline mode does not capture from a device reported lost
        LossCamera cam;
        cam.setup();
        cam.update();
        const int before = cam.captures;
        cam.lose("gone");
        cam.update();
        cam.update();
        check("lifecycle 13. update() leaves a lost device alone", before == 1 && cam.captures == before);
    }
    {
        // The destructor of an open camera
        LossRecorder rec;
        // A camera class with no destructor of its own, so only the base can fire
        auto cam = std::make_unique<SyntheticDepthCamera>(4, 4);
        rec.attach(*cam);
        cam->enableDepth();
        cam->setThreaded(true);
        cam->setup();
        cam.reset();
        check("lifecycle 14. the destructor fires nothing", rec.count == 0);
    }
    {
        // A loss found on the capture thread reaches a Deliver::Main listener once, on the main thread
        LossCamera cam;
        LossRecorder rec;
        rec.attach(cam, Deliver::Main);
        cam.setThreaded(true);
        cam.loseOnCapture("capture thread");
        cam.setup();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (cam.isConnected() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        cam.close();  // joins the worker, so reportLost() has returned
        check("lifecycle 15. the worker reported the loss; the Main listener has not run yet",
              !cam.isConnected() && rec.count == 0);
        internal::drainMainThreadQueue();
        check("lifecycle 16. it runs once on the main thread, with the camera disconnected",
              rec.count == 1 && rec.onMain && !rec.connectedInside && rec.reason == "capture thread");
        internal::drainMainThreadQueue();
        check("lifecycle 17. and not again", rec.count == 1);
    }
}
} // namespace

int main() {
    ownThreadTeardown();
    lifecycle();
    rollback(false);
    rollback(true);
    pendingFrame();
    playback();
    deviceLifecycle();
    return failures == 0 ? 0 : 1;
}
