#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <condition_variable>
#include <cstdio>
#include <mutex>

using namespace tc;
namespace {
struct State {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> calls{0};
    bool gateEntered = false;
    bool releaseGate = false;
    bool barrier = false;
    bool cleaned = false;
};
bool await(State& state, const std::function<bool()>& ready) {
    std::unique_lock<std::mutex> lock(state.mutex);
    return state.cv.wait_for(lock, std::chrono::seconds(30), ready);
}
struct TimedNode : App {
    State& state;
    std::vector<int> member{0};
    explicit TimedNode(State& value) : state(value) {
        // Positive, below the scheduler clock's tick: each reschedule is due
        // before the subsequent gate/barrier. No elapsed-time assertion.
        callEveryAsync(1e-30, [this] {
            std::lock_guard<std::mutex> lock(state.mutex);
            ++member[0];
            ++state.calls;
            state.cv.notify_all();
        });
    }
    void cleanup() override {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.cleaned = true;
    }
};
}
TC_CORE_TEST_MAIN() {
    State state;
    auto root = internal::makeConstructedApp<App>();
    auto node = internal::makeConstructedApp<TimedNode>(state);
    root->addChild(node);
    bool ok = await(state, [&] { return state.calls > 0; });
    Node gate;
    gate.callAfterAsync(0, [&] {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.gateEntered = true;
        state.cv.notify_all();
        state.cv.wait(lock, [&] { return state.releaseGate; });
    });
    ok = await(state, [&] { return state.gateEntered; }) && ok;
    // cleanupTree() is framework-private. Removing a dead child through the
    // real update traversal runs it, including App's pre-cleanup hook chain.
    node->destroy();
    root->handleUpdate(0, 0);
    const int afterCleanup = state.calls;
    // Prove cancellation while the derived member still exists as well as
    // after its destruction. A different owner's barrier advances the worker
    // past the already-due repeating timer without waiting an arbitrary time.
    gate.callAfterAsync(0, [&] {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.barrier = true;
        state.cv.notify_all();
    });
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.releaseGate = true;
        state.cv.notify_all();
    }
    ok = await(state, [&] { return state.barrier; }) && ok;
    ok = state.cleaned && state.calls == afterCleanup && ok;
    // Check before destruction so ~Node's fallback cannot hide a missing
    // pre-cleanup cancellation, then check the scheduler again after destroy.
    root.reset();
    node.reset();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.barrier = false;
    }
    gate.callAfterAsync(0, [&] {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.barrier = true;
        state.cv.notify_all();
    });
    ok = await(state, [&] { return state.barrier; }) && ok;
    ok = state.calls == afterCleanup && ok;
    gate.cancelAllAsyncTimers();
    std::printf("tree cleanup cancels derived-member timer before cleanup/destruction: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
