#include "tc/video/internal/tcVideoNotifyQueue.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <future>
#include <thread>

namespace {
using namespace trussc::internal;
struct MockPlayerState;
struct MockNotify {
    std::weak_ptr<MockPlayerState> target;
    void notify(VideoNotifyEvent event) const;
};
struct MockEngine {
    // Models Media Foundation retaining its notification object.
    std::shared_ptr<MockNotify> notify;
};
struct MockPlayerState {
    VideoNotifyQueue notifications;
    std::shared_ptr<MockEngine> engine;
    int renderTextures = 0;
};
void MockNotify::notify(VideoNotifyEvent event) const { enqueueVideoNotify(target, event); }
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
} // namespace

TC_CORE_TEST_MAIN() {
    auto state = std::make_shared<MockPlayerState>();
    MockNotify notify{state};
    auto retainedEngine = std::make_shared<MockEngine>();
    retainedEngine->notify = std::make_shared<MockNotify>(notify);
    state->engine = retainedEngine;
    const auto mainThread = std::this_thread::get_id();
    std::thread producer([&] {
        notify.notify({1, 23, 45});
        notify.notify({2, 67, 89});
    });
    producer.join();
    check("off-thread notify performs no rendering", state->renderTextures == 0);
    bool mainDelivery = true;
    auto events = state->notifications.take();
    for (const auto& event : events) {
        mainDelivery &= std::this_thread::get_id() == mainThread;
        if (event.event == 1) ++state->renderTextures;
    }
    check("main drain preserves notification order and parameters",
          mainDelivery && state->renderTextures == 1 && events.size() == 2 &&
          events[0].param1 == 23 && events[0].param2 == 45 && events[1].event == 2);
    check("drained notifications delivered exactly once", state->notifications.take().empty());

    notify.notify({3, 0, 0});
    state->notifications.close();
    state->engine.reset(); // close releases the engine after marking State closed
    // A callback holding a live State after close still cannot enqueue.
    auto inFlight = notify.target.lock();
    std::thread late([&] { inFlight->notifications.push({4, 0, 0}); });
    late.join();
    notify.notify({5, 0, 0});
    check("close discards queued and late notifications", state->notifications.take().empty());
    inFlight.reset();
    state.reset();
    check("notify holds no cycle; State expires after close", notify.target.expired());
    retainedEngine->notify->notify({6, 0, 0});
    check("notify after State destruction is safe", notify.target.expired());

    // Race close against enqueue without depending on which thread wins.
    state = std::make_shared<MockPlayerState>();
    MockNotify racing{state};
    std::promise<void> begin;
    auto ready = begin.get_future().share();
    std::thread racingProducer([&] {
        ready.wait();
        for (uint32_t i = 0; i < 256; ++i) racing.notify({i, i, i});
    });
    begin.set_value();
    state->notifications.close();
    racingProducer.join();
    check("concurrent close leaves no deliverable notifications", state->notifications.take().empty());
    return failures ? 1 : 0;
}
