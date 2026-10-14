// Headless regression for capture pointers surviving VideoGrabber moves.
#include <TrussC.h>
#include <mutex>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Keep the accessor in the test; VideoGrabber exposes no new public methods.
namespace trussc::internal {
struct VideoGrabberTestAccess {
    static GrabberSharedState* sharedState(const VideoGrabber& grabber) {
        return grabber.state_.get();
    }
    static std::shared_ptr<GrabberSharedState> captureState(VideoGrabber& grabber) {
        return grabber.state_;
    }
    static void prepare(VideoGrabber& grabber) {
        grabber.initialized_ = true;
        grabber.state_->width = 1;
        grabber.state_->height = 1;
        grabber.state_->pixels = std::make_unique<unsigned char[]>(4);
    }
};
} // namespace trussc::internal

namespace {
using namespace tc;
using Access = internal::VideoGrabberTestAccess;

static_assert(!std::is_copy_constructible_v<VideoGrabber>);
static_assert(!std::is_copy_assignable_v<VideoGrabber>);
static_assert(std::is_nothrow_move_constructible_v<VideoGrabber>);
static_assert(std::is_nothrow_move_assignable_v<VideoGrabber>);

int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Mimic a backend which saves pointers once and never rebinds them on move.
void publishFrame(const std::shared_ptr<internal::GrabberSharedState>& state, uint64_t number) {
    const unsigned char rgba[] = {static_cast<unsigned char>(number), 2, 3, 255};
    {
        std::lock_guard<std::mutex> lock(state->mtx);
        std::memcpy(state->pixels.get(), rgba, sizeof(rgba));
        state->dirty.store(true);
    }
    state->frameQueue.push(rgba, 1, 1, number);
}

void publish(std::mutex* mutex, std::atomic<bool>* dirty) {
    std::lock_guard<std::mutex> lock(*mutex);
    dirty->store(true);
}
#if !defined(__EMSCRIPTEN__)
// Optional GPU check: Xvfb suffices; no camera or physical display is needed.
class TextureTeardownApp : public App {
public:
    void setup() override {
        getMainThreadId();
        auto owner = std::make_unique<VideoGrabber>();
        Access::prepare(*owner);
        owner->getTexture().allocate(1, 1, 4, TextureUsage::Stream);
        const sg_image image = owner->getTexture().getImage();
        check("GPU check allocates a real grabber texture", owner->getTexture().isAllocated());
        auto state = Access::captureState(*owner);
        std::weak_ptr<internal::GrabberSharedState> weak = state;
        std::promise<void> finished;
        state->keptThreads.start(state->captureThread, [state, &owner, &finished] {
            owner.reset();
            finished.set_value();
        });
        finished.get_future().wait();
        state->keptThreads.joinOthers();
        state.reset();
        check("capture State expires before queued GPU cleanup", weak.expired());
        check("texture survives the worker callback", sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        auto cleanup = internal::mainThreadQueue().receiveAll();
        check("worker queues owned texture cleanup on main", cleanup.size() == 1);
        for (auto& fn : cleanup) fn();
        cleanup.clear();
        internal::drainPendingGpuDestroys();
        check("main cleanup releases the grabber texture", sg_query_image_state(image) != SG_RESOURCESTATE_VALID);
        ready_ = true;
    }
    void update() override {
        if (ready_) {
            completed = true;
            exitApp();
        }
    }
    static inline bool completed = false;
private:
    bool ready_ = false;
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if !defined(__EMSCRIPTEN__)
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        runApp<TextureTeardownApp>(settings);
        check("GPU teardown check completes", TextureTeardownApp::completed);
        return failures ? 1 : 0;
    }
#else
    (void)argc;
    (void)argv;
#endif

    VideoGrabber destination;
    internal::GrabberSharedState* state = nullptr;
    {
        VideoGrabber source;
        source.setDeviceID(3);
        source.setDesiredFrameRate(30);
        source.setVerbose(true);
        source.setFrameQueueSize(4);
        state = Access::sharedState(source);
        check("default shared state exists and starts clean", state && !state->dirty.load());
        state->dirty.store(true);
        VideoGrabber moved(std::move(source));
        check("move constructor preserves shared state address", Access::sharedState(moved) == state);
        check("move constructor preserves pending frame", Access::sharedState(moved)->dirty.load());
        check("moved-from state is fresh and independent",
              Access::sharedState(source) && Access::sharedState(source) != state &&
              !Access::sharedState(source)->dirty.load());
        check("move preserves device settings and frame queue",
              moved.getDeviceID() == 3 && moved.getDesiredFrameRate() == 30 &&
              moved.isVerbose() && moved.getFrameQueueSize() == 4);
        check("moved-from grabber is inactive", !source.isInitialized() && !source.isPendingPermission());
        source.setFrameQueueSize(2);
        check("moved-from frame queue can be reused independently", moved.getFrameQueueSize() == 4);

        destination = std::move(moved);
        check("move assignment preserves shared state address", Access::sharedState(destination) == state);
        check("move assignment preserves pending frame", Access::sharedState(destination)->dirty.exchange(false));
        check("move-assigned source has independent state",
              Access::sharedState(moved) && Access::sharedState(moved) != state);
        auto& same = destination;
        destination = std::move(same);
        check("self move assignment preserves shared state", Access::sharedState(destination) == state);
    }

    // Both moved-from objects have been destroyed. A saved capture pointer
    // must still lock a live mutex and notify the destination's dirty flag.
    auto* savedMutex = &state->mtx;
    auto* savedDirty = &state->dirty;
    std::thread capture([=] { publish(savedMutex, savedDirty); });
    capture.join();
    check("saved callback pointers work after source destruction",
          Access::sharedState(destination)->dirty.exchange(false));

    std::vector<VideoGrabber> grabbers;
    grabbers.reserve(1);
    grabbers.push_back(std::move(destination));
    const auto oldCapacity = grabbers.capacity();
    // A worker holds the original pointers while vector relocation happens.
    std::thread vectorCapture([=] {
        for (int i = 0; i < 256; ++i) publish(savedMutex, savedDirty);
    });
    while (grabbers.size() <= oldCapacity) grabbers.emplace_back();
    vectorCapture.join();
    check("vector growth forced reallocation", grabbers.capacity() > oldCapacity);
    check("vector reallocation preserves shared state address", Access::sharedState(grabbers.front()) == state);
    check("vector reallocation preserves capture notifications", Access::sharedState(grabbers.front())->dirty.exchange(false));
    check("vector reallocation preserves frame queue", grabbers.front().getFrameQueueSize() == 4);

    // close() without a camera and subsequent moves leave synchronization
    // available for setup/reuse, including on a previously moved-from handle.
    auto priorState = Access::captureState(grabbers.front());
    grabbers.front().close();
    destination = std::move(grabbers.front());
    check("close and a later move use a fresh state", Access::sharedState(destination) != state);
    check("moved-from handle remains usable", Access::sharedState(grabbers.front()) != nullptr);

    // A callback is held in flight across both moves and source destruction.
    // Promise gates force the interleaving; elapsed time is never asserted.
    VideoGrabber receiving;
    std::promise<void> entered, resume;
    auto resumeFuture = resume.get_future().share();
    {
        VideoGrabber source;
        Access::prepare(source);
        source.setFrameQueueSize(8);
        auto captureState = Access::captureState(source);
        captureState->keptThreads.start(captureState->captureThread,
            [captureState, &entered, resumeFuture] {
                publishFrame(captureState, 1);
                entered.set_value();
                resumeFuture.wait();
                publishFrame(captureState, 2);
                publishFrame(captureState, 3);
            });
        entered.get_future().wait();
        VideoGrabber moved(std::move(source));
        receiving = std::move(moved);
    }
    resume.set_value();
    auto receivingState = Access::captureState(receiving);
    receivingState->keptThreads.release(receivingState->captureThread);
    std::vector<GrabberFrame> received;
    receiving.getQueuedFrames(received);
    check("in-flight move retains every queued frame in order",
          received.size() == 3 && received[0].timestampUs == 1 &&
          received[1].timestampUs == 2 && received[2].timestampUs == 3 &&
          received[2].pixels.getData()[0] == 3);
    check("move keeps capture active and updates destination pixels",
          !receivingState->stopped.load() && receiving.getPixels()[0] == 3);

    VideoGrabber independent;
    Access::prepare(independent);
    independent.setFrameQueueSize(8);
    auto independentState = Access::captureState(independent);
    std::thread first([receivingState] { publishFrame(receivingState, 4); });
    std::thread second([independentState] { publishFrame(independentState, 9); });
    first.join();
    second.join();
    std::vector<GrabberFrame> otherFrames;
    independent.getQueuedFrames(otherFrames);
    received.clear();
    receiving.getQueuedFrames(received);
    check("two live grabbers publish independent pixels and queues",
          received.size() == 1 && received[0].timestampUs == 4 &&
          otherFrames.size() == 1 && otherFrames[0].timestampUs == 9 &&
          receiving.getPixels()[0] == 4 && independent.getPixels()[0] == 9);

    std::weak_ptr<internal::GrabberSharedState> oldRun = independentState;
    independent.close();
    check("close stops old State and replaces the handle's generation",
          independentState->stopped.load() &&
          Access::captureState(independent) != independentState);
    independentState.reset();
    check("closed run is released after its last worker reference", oldRun.expired());

    auto selfDestroyed = std::make_unique<VideoGrabber>();
    Access::prepare(*selfDestroyed);
    selfDestroyed->setFrameQueueSize(8);
    auto selfState = Access::captureState(*selfDestroyed);
    std::weak_ptr<internal::GrabberSharedState> selfWeak = selfState;
    std::promise<void> selfFinished;
    bool stopObserved = false;
    selfState->keptThreads.start(selfState->captureThread,
        [state = selfState, &selfDestroyed, &selfFinished, &stopObserved]() mutable {
            selfDestroyed.reset();  // user's fake frame callback
            stopObserved = state->stopped.load();
            // Callback still owns pixels, FIFO, mutex and platform resources.
            publishFrame(state, 7);
            state.reset();
            selfFinished.set_value();
        });
    selfState.reset();
    selfFinished.get_future().wait();
    check("destroy in frame callback returns and retains State until exit",
          stopObserved && !selfDestroyed && selfWeak.expired());

    VideoGrabber closing;
    Access::prepare(closing);
    auto closingState = Access::captureState(closing);
    std::promise<void> closingEntered, finishCallback;
    auto finishFuture = finishCallback.get_future().share();
    bool callbackReturned = false;
    closingState->keptThreads.start(closingState->captureThread,
        [closingState, &closingEntered, finishFuture, &callbackReturned] {
            closingEntered.set_value();
            finishFuture.wait();
            publishFrame(closingState, 5);
            callbackReturned = true;
        });
    closingEntered.get_future().wait();
    bool joinedBeforeCloseReturned = false;
    std::thread stopper([&] {
        closing.close();
        joinedBeforeCloseReturned = callbackReturned;
    });
    finishCallback.set_value();
    stopper.join();
    check("close on another thread joins the in-flight callback",
          joinedBeforeCloseReturned && closingState->stopped.load());

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
