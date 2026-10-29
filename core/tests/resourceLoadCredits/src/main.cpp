#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <climits>

using namespace tc;
namespace {
using namespace std;
#include "../../asyncLoads/src/fontFixture.h"
int failures = 0;
void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
void settle() { internal::waitForResourceLoadsForTests(); drain(); }
struct State : internal::ResourceLoadState {
    std::vector<unsigned char> data;
    size_t requestedBytes = 0;
    std::function<void()> before;
    size_t retainedBytes() const noexcept override { return data.capacity(); }
    void discardPayload() noexcept override { std::vector<unsigned char>().swap(data); }
};
struct Load {
    Event<LoadResult> event;
    internal::ResourceLoadSlot<State> slot;
    int events = 0;
    LoadError error = LoadError::None;
    EventListener listener = event.listen([this](LoadResult& result) {
        ++events; error = result.error;
        check("notification is on main after application", isMainThread() && !slot.loading());
    });
    std::shared_ptr<State> begin() { return slot.begin(event, [](State&) {}); }
    std::shared_ptr<State> start(size_t bytes, std::function<void()> before = {}) {
        auto state = begin();
        state->requestedBytes = bytes;
        state->before = std::move(before);
        internal::startExternalResourceLoad(state, [](const auto& ready) {
            if (ready->before) ready->before();
            ready->data.resize(ready->requestedBytes);
        });
        return state;
    }
};
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    size_t entered = 0;
    bool opened = false;
    void block() {
        std::unique_lock lock(mutex);
        ++entered; changed.notify_all();
        changed.wait(lock, [&] { return opened; });
    }
    void wait(size_t count) {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(60), [&] { return entered >= count; })) std::_Exit(5);
    }
    void open() { std::lock_guard lock(mutex); opened = true; changed.notify_all(); }
};
#include "../../common/resourceLoadLocalChecks.h"
void concurrency() {
    check("default preparation concurrency is four", getResourceLoadConcurrency() == 4);
    setResourceLoadConcurrency(8);
    check("public setter accepts eight", getResourceLoadConcurrency() == 8);
    Gate first, queued;
    std::vector<std::unique_ptr<Load>> loads;
    auto add = [&](Gate& gate) {
        auto load = std::make_unique<Load>();
        load->start(1, [&gate] { gate.block(); });
        loads.push_back(std::move(load));
    };
    for (int i = 0; i < 8; ++i) add(first);
    first.wait(8);
    add(queued); add(queued);
    check("eight preparations run and excess work queues", internal::resourceLoadUsageForTests().active == 8 &&
        internal::resourceLoadUsageForTests().pending == 2);
    setResourceLoadConcurrency(10);
    queued.wait(2); // No original worker has been released.
    check("raising immediately pumps queued preparations", internal::resourceLoadUsageForTests().active == 10 &&
        internal::resourceLoadUsageForTests().pending == 0);
    setResourceLoadConcurrency(1);
    Gate afterLowering;
    add(afterLowering);
    check("lowering keeps running work and queues new work", internal::resourceLoadUsageForTests().active == 10 &&
        internal::resourceLoadUsageForTests().pending == 1);
    first.open();
    // Wait for the eight finished workers to retire, while two remain blocked.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (internal::resourceLoadUsageForTests().active != 2) {
        if (std::chrono::steady_clock::now() >= deadline) std::_Exit(5);
        std::this_thread::yield();
    }
    check("lowered limit prevents workers from taking queued work", internal::resourceLoadUsageForTests().pending == 1);
    queued.open();
    afterLowering.wait(1);
    check("queued work resumes when running work falls below limit", internal::resourceLoadUsageForTests().active == 1);
    afterLowering.open(); settle();
    bool succeeded = true;
    for (auto& load : loads) succeeded &= load->events == 1 && load->error == LoadError::None;
    check("lowering never cancels running or queued requests", succeeded);

    int notices = 0;
    auto log = getLogger().onLog.listen([&](LogEventArgs& message) {
        if (message.level == LogLevel::Notice && message.message.find("[ResourceLoad]") != std::string::npos) ++notices;
    });
    setResourceLoadConcurrency(0);
    check("zero becomes one with exactly one Notice", getResourceLoadConcurrency() == 1 && notices == 1);
    setResourceLoadConcurrency(-8);
    check("negative becomes one with exactly one Notice", getResourceLoadConcurrency() == 1 && notices == 2);
    setResourceLoadConcurrency(INT_MAX);
    check("no upper cap or Notice for valid counts", getResourceLoadConcurrency() == INT_MAX && notices == 2);

    setResourceLoadConcurrency(8);
    Gate budget;
    loads.clear();
    for (int i = 0; i < 68; ++i) add(budget);
    budget.wait(8);
    Load refused;
    refused.start(1); drain();
    check("public concurrency leaves external live-request budget unchanged", refused.events == 1 &&
        refused.error == LoadError::TooLarge && internal::resourceLoadUsageForTests().pending == 60);
    budget.open(); settle();
    setResourceLoadConcurrency(4);
}
void credits() {
    internal::setResourceLoadLimitsForTests(4, 64, 64);
    Load first, second, overflow;
    auto a = first.start(32);
    auto b = second.start(32);
    internal::waitForResourceLoadsForTests();
    check("completed results retain exact credits until main applies", internal::resourceLoadUsageForTests().retainedBytes == 64 && first.events == 0);
    auto snapshot = internal::takeResourceLoadBatch();
    check("taking a snapshot does not release payload credits", internal::resourceLoadUsageForTests().retainedBytes == 64);
    const auto rejected = internal::resourceLoadUsageForTests().rejected;
    std::atomic<int> visits{0};
    overflow.start(1, [&] { ++visits; });
    settle();
    check("full credits reject before preparation and settle once", visits == 0 && overflow.events == 1 && overflow.error == LoadError::TooLarge && internal::resourceLoadUsageForTests().rejected == rejected + 1);
    first.slot.cancel();
    check("cancel releases retained backing storage before snapshot drain", a->data.capacity() == 0 && internal::resourceLoadUsageForTests().retainedBytes == 32);
    internal::deliverResourceLoadBatch(std::move(snapshot));
    check("cancelled owner receives no stale event; other owner settles", first.events == 0 && second.events == 1 && internal::resourceLoadUsageForTests().retainedBytes == 0);
    drain(); check("no duplicate failure after another drain", overflow.events == 1);

    Load partial, tooBig;
    partial.start(48); internal::waitForResourceLoadsForTests();
    auto c = tooBig.start(32); internal::waitForResourceLoadsForTests();
    check("aggregate admission rejects whole completion and frees its storage", c->data.capacity() == 0 && internal::resourceLoadUsageForTests().retainedBytes == 48);
    drain();
    check("completion admission failure is delivered once", tooBig.events == 1 && tooBig.error == LoadError::TooLarge && partial.events == 1);

    internal::setResourceLoadLimitsForTests(1, 1, 1024);
    Load tinyFirst, tinySecond, tinyOverflow;
    tinyFirst.start(1); internal::waitForResourceLoadsForTests();
    tinySecond.start(1); internal::waitForResourceLoadsForTests();
    visits = 0;
    tinyOverflow.start(1, [&] { ++visits; });
    check("completed tiny payloads retain request credits as well as bytes", internal::resourceLoadUsageForTests().outstanding == 2 && internal::resourceLoadUsageForTests().retainedBytes == 2 && visits == 0);
    drain();
    check("request-credit overflow still settles once and all credits return", tinyOverflow.events == 1 && tinyOverflow.error == LoadError::TooLarge && tinyFirst.events == 1 && tinySecond.events == 1 && internal::resourceLoadUsageForTests().outstanding == 0);
    internal::setResourceLoadLimitsForTests(4, 64, 64);

    Load duplicate;
    auto d = duplicate.begin(); d->data.resize(64);
    internal::completeResourceLoad(d); internal::completeResourceLoad(d);
    drain();
    check("duplicate backend completion cannot double-charge or notify", duplicate.events == 1 && internal::resourceLoadUsageForTests().retainedBytes == 0);

    {
        Load owner;
        owner.start(64); internal::waitForResourceLoadsForTests();
    }
    check("owner destruction immediately returns credits", internal::resourceLoadUsageForTests().retainedBytes == 0);
    drain();
    Load discarded;
    auto e = discarded.start(64); internal::waitForResourceLoadsForTests();
    { auto batch = internal::takeResourceLoadBatch(); }
    check("discarding a batch releases payload even if owner survives", e->data.empty() && !discarded.slot.loading() && internal::resourceLoadUsageForTests().retainedBytes == 0 && discarded.events == 0);
}
void workersAndFailures() {
    internal::setResourceLoadLimitsForTests(4, 64, 1024);
    Gate gate;
    std::vector<std::unique_ptr<Load>> loads;
    for (int i = 0; i < 4; ++i) {
        auto load = std::make_unique<Load>(); load->start(1, [&] { gate.block(); }); loads.push_back(std::move(load));
    }
    gate.wait(4);
    for (int i = 0; i < 64; ++i) {
        auto load = std::make_unique<Load>(); load->start(1); loads.push_back(std::move(load));
    }
    const auto usage = internal::resourceLoadUsageForTests();
    Load refused;
    refused.start(1);
    drain();
    check("four active decodes and sixty-four pending requests; next fails", usage.active == 4 && usage.pending == 64 && refused.events == 1 && refused.error == LoadError::TooLarge);
    loads.back()->slot.cancel();
    check("pending cancellation removes its captured preparation", internal::resourceLoadUsageForTests().pending == 63);
    std::atomic<int> appJobs{0};
    internal::startResourceLoad([&] { ++appJobs; });
    internal::enqueueResourceLoad([&] { ++appJobs; });
    drain();
    gate.open(); settle();
    size_t delivered = 0;
    for (auto& load : loads) delivered += load->events;
    check("all accepted requests settle except explicitly cancelled owner", delivered == 67 && internal::resourceLoadUsageForTests().active == 0 && internal::resourceLoadUsageForTests().pending == 0);
    check("resource saturation does not cap app-authored jobs", appJobs == 2);

    Load launchFailure;
    internal::failResourceLoadLaunchForTests(true);
    launchFailure.start(1);
    internal::failResourceLoadLaunchForTests(false);
    settle();
    check("allocation/thread-launch failure settles without a stuck worker", launchFailure.events == 1 && launchFailure.error == LoadError::Unknown && internal::resourceLoadUsageForTests().active == 0);
    Load thrown;
    auto state = thrown.begin();
    internal::startExternalResourceLoad(state, [](const auto& ready) { ready->data.resize(32); throw std::bad_alloc(); });
    settle();
    check("preparation allocation failure is clean and frees storage", thrown.events == 1 && thrown.error == LoadError::Unknown && state->data.empty());
    Load applyFailure;
    auto ready = applyFailure.slot.begin(applyFailure.event, [](State&) { throw std::bad_alloc(); });
    ready->data.resize(32);
    internal::completeResourceLoad(ready);
    drain();
    check("application allocation failure settles once and returns credits", applyFailure.events == 1 && applyFailure.error == LoadError::Unknown && ready->data.empty() && internal::resourceLoadUsageForTests().retainedBytes == 0);
    Load applying;
    bool observed = false;
    auto inMain = applying.slot.begin(applying.event, [&](State& ready) {
        std::thread workerExit([&] { ready.cancel(); });
        workerExit.join();
        check("worker-exit cancellation leaves an applying payload to main", ready.data.size() == 32 && internal::resourceLoadUsageForTests().retainedBytes == 32);
        observed = true;
    });
    internal::startExternalResourceLoad(inMain, [](const auto& ready) { ready->data.resize(32); });
    settle();
    check("main retirement returns credits after concurrent exit cancellation", observed && inMain->data.empty() && internal::resourceLoadUsageForTests().retainedBytes == 0 && internal::resourceLoadUsageForTests().outstanding == 0);
}
void callers() {
    const auto root = fs::absolute("resourceLoadCredits-fixtures");
    fs::create_directories(root);
    Pixels pixels; pixels.allocate(4, 4, 4);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) pixels.setColor(x, y, Color(1, 1, 1));
    check("generated image fixture", pixels.save(root / "image.png").ok());
    const auto fontBytes = makeFont();
    std::ofstream(root / "font.ttf", std::ios::binary).write(reinterpret_cast<const char*>(fontBytes.data()), fontBytes.size());
    // One mono PCM16 frame, built here rather than using a codec/tool fixture.
    unsigned char wave[] = {
        'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,
        'd','a','t','a',2,0,0,0,0,0
    };
    std::ofstream(root / "sound.wav", std::ios::binary).write(reinterpret_cast<const char*>(wave), sizeof(wave));
    internal::setResourceLoadLimitsForTests(4, 64, 1024 * 1024);
    Image image;
    image.loadAsync(root / "image.png"); internal::waitForResourceLoadsForTests();
    check("Image caller charges decoded pixels", internal::resourceLoadUsageForTests().retainedBytes == 64);
    image.clear();
    check("Image clear returns credits before notification", internal::resourceLoadUsageForTests().retainedBytes == 0);
    drain();
    SoundBuffer sound;
    sound.loadAsync(root / "sound.wav"); internal::waitForResourceLoadsForTests();
    check("SoundBuffer caller charges retained PCM capacity", internal::resourceLoadUsageForTests().retainedBytes >= sizeof(float));
    sound.generateSilence(0);
    check("SoundBuffer cancellation returns credits", internal::resourceLoadUsageForTests().retainedBytes == 0);
    drain();
    Font font;
    font.loadAsync(root / "font.ttf", 16); internal::waitForResourceLoadsForTests();
    check("Font caller charges file bytes and initial CPU atlas", internal::resourceLoadUsageForTests().retainedBytes >= fontBytes.size() + 256 * 256);
    drain();
    font.loadAsync(root / "font.ttf", 16);
    check("committed font cache hits do not charge new decoded storage", internal::resourceLoadUsageForTests().retainedBytes == 0);
    drain();
    internal::setResourceLoadLimitsForTests(4, 64, 1);
    int events = 0, errors = 0;
    const auto rejected = internal::resourceLoadUsageForTests().rejected;
    auto onImage = image.onLoad.listen([&](LoadResult& result) { ++events; if (!result) ++errors; });
    auto onSound = sound.onLoad.listen([&](LoadResult& result) { ++events; if (!result) ++errors; });
    auto onFont = font.onLoad.listen([&](LoadResult& result) { ++events; if (!result) ++errors; });
    image.loadAsync(root / "image.png"); sound.loadAsync(root / "sound.wav"); font.loadAsync(root / "font.ttf", 17);
    settle();
    check("all local callers keep successful results above byte credits", events == 3 && errors == 0 &&
        image.getPixels().getWidth() == 4 && font.isLoaded() && !sound.samples.empty() &&
        internal::resourceLoadUsageForTests().retainedBytes == 0 && internal::resourceLoadUsageForTests().rejected == rejected);
    fs::remove_all(root);
}
void lifecycle() {
    internal::setResourceLoadLimitsForTests(4, 64, 64);
    Load owner;
    auto state = owner.start(64); internal::waitForResourceLoadsForTests();
    internal::waitForResourceLoadsAtAppExit();
    check("app exit/hot reload cancels retained results before context teardown", state->data.capacity() == 0 && internal::resourceLoadUsageForTests().retainedBytes == 0 && !owner.slot.loading());
    drain(); check("teardown never dispatches a stale guest callback", owner.events == 0);
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    Load replacement;
    replacement.start(64); settle();
    check("next app run can reuse all credits", replacement.events == 1 && replacement.error == LoadError::None);
}
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    int warnings = 0;
    auto log = getLogger().onLog.listen([&](LogEventArgs& message) {
        if (message.message.find("[ResourceLoad]") != std::string::npos && message.level == LogLevel::Warning) ++warnings;
    });
    concurrency(); localFileAdmissionChecks(); credits(); workersAndFailures(); callers(); lifecycle();
    settle();
    check("one first admission warning across all repeated failures", warnings == 1);
    internal::setResourceLoadLimitsForTests(4, 64, size_t(1024) * 1024 * 1024);
    return failures ? 1 : 0;
}
