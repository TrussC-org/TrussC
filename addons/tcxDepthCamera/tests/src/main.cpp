// Headless depth-camera lifecycle and frame-publication regressions.
// Built by examples/build_all.py --addon-tests-only.
#include <tcxDepthCamera.h>
#include <tcxDepthRecord.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
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

struct State {
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
    bool stoppedBeforeMembers = false;

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
    explicit CountingCamera(State& state) : state_(state), witness_{state} {}
    ~CountingCamera() override { close(); }

protected:
    bool openDevice() override {
        if (!state_.canOpen) return false;
        state_.open = true;
        ++state_.opens;
        call_ = 0;
        return true;
    }
    void closeDevice() override {
        if (state_.active != 0) state_.closedWhileActive = true;
        if (state_.open) {
            state_.open = false;
            ++state_.closes;
        }
    }
    StreamFreshness captureInto(DepthFrame& dst) override {
        ++state_.active;
        if (!state_.open) state_.capturedWhileClosed = true;
        const int call = ++call_;
        if (isThreaded()) {
            std::unique_lock lock(state_.mutex);
            ++state_.entered;
            state_.changed.notify_all();
            // Poll the stop flag only to let close() join a gated capture.
            // Frame advancement is controlled by allow(), never by a sleep.
            while (state_.allowed < state_.entered && isThreadRunning())
                state_.changed.wait_for(lock, std::chrono::milliseconds(1));
        }
        StreamFreshness fresh;
        if ((call % 2) != 0) {
            dst.w = dst.h = 1;
            dst.depth.assign(1, static_cast<uint16_t>(call));
            dst.timestamp = call;
            fresh.depth = true;
        }
        --state_.active;
        return fresh;
    }

private:
    struct Witness {
        State& state;
        ~Witness() { state.stoppedBeforeMembers = state.active == 0 && !state.open; }
    };
    State& state_;
    Witness witness_;
    int call_ = 0;
};

void lifecycle() {
    State state;
    {
        CountingCamera camera(state);
        camera.setThreaded(true);
        check("first setup", camera.setup());
        check("first worker entered capture", state.waitEntered(1));
        check("setup while capture is active", camera.setup());
        check("setup closed the old session before reopening", state.opens == 2 && state.closes == 1);
        check("reopened worker entered capture", state.waitEntered(2));
        state.canOpen = false;
        check("failed setup closes the old session", !camera.setup() && !state.open && state.active == 0);
        state.canOpen = true;
        camera.close();
        camera.close();
        check("repeated close balances successful opens", state.opens == state.closes);
        check("capture never overlaps a closed device", !state.capturedWhileClosed && !state.closedWhileActive);
        check("setup after repeated close", camera.setup());
        check("worker active before implicit teardown", state.waitEntered(3));
        // No explicit close(): the backend destructor must join first.
    }
    check("destructor closes every session", state.opens == 3 && state.closes == 3);
    check("destructor stops capture before backend members die", state.stoppedBeforeMembers);
}

void rollback(bool threaded) {
    State state;
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
    State state;
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
    GatedPlayback(const std::string& path, State& state)
        : PlaybackDepthCamera(path), state_(state) {}
    ~GatedPlayback() override { close(); }
protected:
    StreamFreshness captureInto(DepthFrame& dst) override {
        if (isThreaded()) {
            std::unique_lock lock(state_.mutex);
            ++state_.entered;
            state_.changed.notify_all();
            while (state_.allowed < state_.entered && isThreadRunning())
                state_.changed.wait_for(lock, std::chrono::milliseconds(1));
            if (!isThreadRunning()) return {};
        }
        return PlaybackDepthCamera::captureInto(dst);
    }
private:
    State& state_;
};

void playback() {
    // Keep the fixture inside the test working directory and remove it at exit.
    const auto path = std::filesystem::absolute("depthcamera-lifecycle.tcdc");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
    } cleanup{path};
    {
        State state;
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
        State state;
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
} // namespace

int main() {
    std::atomic<int> fallbackErrors{0};
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& args) {
        if (args.message.find("backend must call close() in its destructor") != std::string::npos)
            ++fallbackErrors;
    });
    lifecycle();
    rollback(false);
    rollback(true);
    pendingFrame();
    playback();
    check("backend teardown never reaches the base fallback", fallbackErrors == 0);
    return failures == 0 ? 0 : 1;
}
