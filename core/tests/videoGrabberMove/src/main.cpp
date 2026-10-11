// Headless regression for capture pointers surviving VideoGrabber moves.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Keep the accessor in the test; VideoGrabber exposes no new public methods.
namespace trussc::internal {
struct VideoGrabberTestAccess {
    static GrabberSharedState* sharedState(const VideoGrabber& grabber) {
        return grabber.sharedState_.get();
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
void publish(std::mutex* mutex, std::atomic<bool>* dirty) {
    std::lock_guard<std::mutex> lock(*mutex);
    dirty->store(true);
}
} // namespace

TC_CORE_TEST_MAIN() {
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
    grabbers.front().close();
    destination = std::move(grabbers.front());
    check("close and a later move preserve shared state", Access::sharedState(destination) == state);
    check("moved-from handle remains usable", Access::sharedState(grabbers.front()) != nullptr);

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
