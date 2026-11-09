// Fake capture source uses the same retained run and dispatch as native backends.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "../../common/tcVideoGrabberTestAccess.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <future>
#include <thread>

namespace {
using namespace tc;
using Access = internal::VideoGrabberTestAccess;
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void publish(const std::shared_ptr<internal::GrabberSharedState>& state,
             const unsigned char* rgba, int width, int height, uint64_t stamp) {
    state->notifyFrame(rgba, width, height, stamp);
    {
        std::lock_guard<std::mutex> lock(state->mtx);
        if (state->pixels && state->width == width && state->height == height) {
            std::memcpy(state->pixels.get(), rgba, size_t(width) * height * 4);
            state->dirty.store(true);
        }
    }
    state->frameQueue.push(rgba, width, height, stamp);
}

void testFrames() {
    for (size_t capacity : {size_t(0), size_t(2)}) {
        for (bool listening : {false, true}) {
            VideoGrabber grabber;
            Access::prepare(grabber);
            grabber.setFrameQueueSize(capacity);
            auto state = Access::captureState(grabber);
            unsigned char rgba[] = {0, 2, 3, 255};
            int calls = 0;
            bool borrowed = true, beforePublish = true, unlocked = true;
            bool captureThread = true, metadata = true;
            const auto mainId = std::this_thread::get_id();
            EventListener listener;
            if (listening) listener = grabber.onFrame.listen([&](const VideoFrameView& frame) {
                ++calls;
                borrowed &= frame.pixels == rgba;
                captureThread &= std::this_thread::get_id() != mainId;
                metadata &= frame.width == 1 && frame.height == 1 && frame.byteSize == 4 &&
                            frame.channels == 4 && frame.format == PixelFormat::U8 &&
                            frame.timestampUs == uint64_t(calls) && frame.pixels[0] == calls;
                const bool pixelLock = state->mtx.try_lock();
                const bool queueLock = state->frameQueue.mtx.try_lock();
                unlocked &= pixelLock && queueLock;
                beforePublish &= !state->dirty.load() &&
                    state->frameQueue.frames.size() == (capacity ? size_t(calls - 1) : 0);
                if (pixelLock) state->mtx.unlock();
                if (queueLock) state->frameQueue.mtx.unlock();
            });
            std::thread worker([&] {
                for (int index = 1; index <= 3; ++index) {
                    rgba[0] = static_cast<unsigned char>(index);
                    state->dirty.store(false);
                    publish(state, rgba, 1, 1, index);
                }
            });
            worker.join();
            check("one capture-thread call per converted frame", calls == (listening ? 3 : 0) && captureThread);
            check("borrowed pointer, RGBA8 dimensions and arrival stamps", borrowed && metadata);
            check("callback precedes publication and FIFO outside locks", beforePublish && unlocked);
            std::vector<GrabberFrame> frames;
            grabber.getQueuedFrames(frames);
            check("FIFO capacity/overflow unchanged with and without listeners",
                  capacity ? (frames.size() == 2 && frames[0].timestampUs == 2 &&
                              frames[1].timestampUs == 3 && frames[0].pixels.getData()[0] == 2 &&
                              frames[1].pixels.getData()[0] == 3) : frames.empty());
        }
    }
}

void testListeners() {
    VideoGrabber grabber;
    auto state = Access::captureState(grabber);
    const unsigned char rgba[8] = {};
    int first = 0, second = 0, added = 0;
    bool resized = false;
    EventListener later, addition;
    auto earlier = grabber.onFrame.listen([&](const VideoFrameView& frame) {
        ++first;
        resized = frame.width == 2 && frame.height == 1 && frame.byteSize == 8;
        later.disconnect();
        if (!addition.isConnected()) addition = grabber.onFrame.listen([&](const VideoFrameView&) { ++added; });
    }, -1);
    later = grabber.onFrame.listen([&](const VideoFrameView&) { ++second; });
    state->notifyFrame(rgba, 2, 1, 10);
    check("removed later listener skipped; additions start next frame", first == 1 && second == 0 && added == 0);
    state->notifyFrame(rgba, 2, 1, 11);
    check("multiple listeners and actual capture dimensions", first == 2 && added == 1 && resized);
    earlier.disconnect();
    addition.disconnect();
    state->notifyFrame(rgba, 2, 1, 12);
    check("RAII removal ends delivery", first == 2 && added == 1);

    int scopedCalls = 0;
    {
        auto scoped = grabber.onFrame.listen([&](const VideoFrameView&) { ++scopedCalls; });
        state->notifyFrame(rgba, 2, 1, 12);
    }
    state->notifyFrame(rgba, 2, 1, 12);
    check("scope exit disconnects the RAII listener", scopedCalls == 1);

    int retained = 0;
    auto persistent = grabber.onFrame.listen([&](const VideoFrameView&) { ++retained; });
    grabber.close();
    state->notifyFrame(rgba, 2, 1, 13);
    Access::captureState(grabber)->notifyFrame(rgba, 2, 1, 14);
    check("close retains listeners on a new run; stopped run is silent", retained == 1);
    // setup() begins with the same close()/new-run operation; fake the backend
    // afterward so this check never needs a camera or graphics context.
    grabber.close();
    Access::prepare(grabber);
    Access::captureState(grabber)->notifyFrame(rgba, 2, 1, 15);
    check("listener receives frames after restart", retained == 2);
    VideoGrabber destination;
    int destinationCalls = 0;
    auto destinationListener = destination.onFrame.listen([&](const VideoFrameView&) { ++destinationCalls; });
    state = Access::captureState(grabber);
    destination = std::move(grabber);
    state->notifyFrame(rgba, 2, 1, 16);
    Access::captureState(grabber)->notifyFrame(rgba, 2, 1, 17);
    check("move assignment keeps subscriptions with original handles", retained == 3 && destinationCalls == 1);
    VideoGrabber moved(std::move(destination));
    state->notifyFrame(rgba, 2, 1, 18);
    Access::captureState(destination)->notifyFrame(rgba, 2, 1, 19);
    check("move constructor keeps subscriptions with original handles", destinationCalls == 2);

    VideoGrabber receiver;
    int movedCalls = 0;
    auto receiverListener = receiver.onFrame.listen([&](const VideoFrameView&) { ++movedCalls; });
    {
        VideoGrabber source;
        state = Access::captureState(source);
        receiver = std::move(source);
    }
    state->notifyFrame(rgba, 2, 1, 20);
    check("retained run dispatches after the moved-from handle is destroyed", movedCalls == 1);
}

void testCloseBarrier() {
    VideoGrabber grabber;
    auto state = Access::captureState(grabber);
    std::promise<void> entered, resume;
    auto resumeFuture = resume.get_future();
    bool returned = false;
    auto listener = grabber.onFrame.listen([&](const VideoFrameView&) {
        entered.set_value();
        resumeFuture.wait();
        returned = true;
    });
    state->keptThreads.start(state->captureThread, [state] {
        const unsigned char rgba[4] = {};
        state->notifyFrame(rgba, 1, 1, 1);
    });
    entered.get_future().wait();
    bool waited = false;
    std::thread closing([&] { grabber.close(); waited = returned; });
    // close() stores stopped before joining. Hold the callback until the
    // close operation has reached that point; no elapsed-time assertion.
    while (!state->stopped.load()) std::this_thread::yield();
    resume.set_value();
    closing.join();
    check("close waits for an executing onFrame listener", waited);
}

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
// Manual native camera check, separate from the default headless regression.
class CameraCheckApp : public App {
public:
    static inline int deviceId = 0;
    static inline bool completed = false;
    void setup() override {
        grabber.setDeviceID(deviceId);
        grabber.setFrameQueueSize(512);
        listener = grabber.onFrame.listen([this](const VideoFrameView& frame) {
            std::lock_guard<std::mutex> lock(recordsMutex);
            records.push_back({frame.timestampUs, frame.width, frame.height,
                               frame.byteSize, frame.pixels[0]});
        });
        startRun();
    }
    void cleanup() override {
        grabber.close(); // Join callbacks before callback-owned records are destroyed.
    }
    void update() override {
        grabber.update();
        if (!grabber.isConnected() && !grabber.isPendingPermission()) {
            check("camera opened and remains connected", false);
            completed = true;
            exitApp();
            return;
        }
        std::vector<GrabberFrame> frames;
        grabber.getQueuedFrames(frames);
        for (const auto& frame : frames) {
            if (received == 120) break;
            checkFrame(frame);
            ++received;
        }
        if (received != 120) return;
        grabber.close(); // barrier before inspecting callback-owned state
        size_t callbackCount;
        {
            std::lock_guard<std::mutex> lock(recordsMutex);
            callbackCount = records.size();
        }
        check("device run has 120 FIFO samples", received == 120);
        check("callback count matches enabled/disabled mode",
              phase < 2 ? callbackCount >= 120 : callbackCount == 0);
        std::printf("DEVICE phase=%d fifo=%d callbacks=%zu stamps/data=%s\n",
                    phase, received, callbackCount, valid ? "PASS" : "FAIL");
        check("monotonic stamps and callback/FIFO data match", valid);
        if (++phase == 3) {
            completed = true;
            exitApp();
            return;
        }
        if (phase == 2) listener.disconnect();
        // Phase 1 keeps the same listener across close/setup; phase 2 has none.
        startRun();
    }
private:
    struct Record { uint64_t stamp; int width, height; size_t bytes; unsigned char firstByte; };
    VideoGrabber grabber;
    EventListener listener;
    std::mutex recordsMutex;
    std::vector<Record> records;
    int phase = 0, received = 0;
    uint64_t previous = 0;
    bool valid = true;
    void startRun() {
        {
            std::lock_guard<std::mutex> lock(recordsMutex);
            records.clear();
        }
        received = 0;
        previous = 0;
        valid = true;
        grabber.setup(640, 480);
    }
    void checkFrame(const GrabberFrame& frame) {
        valid &= frame.timestampUs >= previous && frame.timestampUs != 0;
        previous = frame.timestampUs;
        if (phase >= 2) return;
        std::lock_guard<std::mutex> lock(recordsMutex);
        if (records.size() <= size_t(received)) { valid = false; return; }
        const auto& record = records[received];
        valid &= record.stamp == frame.timestampUs &&
                 record.width == frame.pixels.getWidth() &&
                 record.height == frame.pixels.getHeight() &&
                 record.bytes == size_t(record.width) * record.height * 4 &&
                 record.firstByte == frame.pixels.getData()[0];
    }
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (argc > 1 && std::strcmp(argv[1], "--device-check") == 0) {
        if (argc > 2) CameraCheckApp::deviceId = std::atoi(argv[2]);
        WindowSettings settings;
        settings.setSize(64, 64);
        return tcCoreTest::runDisplayTest<CameraCheckApp>(settings, failures, [] {
            check("all three native camera runs complete", CameraCheckApp::completed);
        });
    }
#else
    (void)argc;
    (void)argv;
#endif
    testFrames();
    testListeners();
    testCloseBarrier();
    return failures ? 1 : 0;
}
