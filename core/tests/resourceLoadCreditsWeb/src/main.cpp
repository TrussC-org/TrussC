#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
#include <condition_variable>

// Owned binary: exercise real allocation failure, including on MSVC.
namespace {
thread_local int failAfter = -1;
thread_local bool failImageCopyStorage = false;
// Refuse the RGBA copy after the encoded snapshot and stb decode exist.
constexpr size_t imageCopyBytes = 8 * 8 * 4;
int allocationFaults = 0;
std::atomic<bool> trackFontStorage{false};
size_t fontStorageSize = 0;
std::atomic<void*> fontStorage{nullptr};
std::atomic<void*> fontAtlas{nullptr};
constexpr size_t encodedImageSize = 4093; // Distinct from filebuf's 4096-byte reads.
std::atomic<bool> trackImageStorage{false};
std::atomic<void*> imageStorage{nullptr};
std::atomic<int> imageStorageAllocations{0};
void trackAllocation(void* p, size_t n) {
    if (trackFontStorage && n == fontStorageSize) fontStorage = p;
    // MSVC's large-block allocator adds alignment metadata. Save the raw
    // pointer returned here: allocator::deallocate passes that pointer back.
    if (trackFontStorage && n >= 65536 && n <= 65536 + 64) fontAtlas = p;
    if (trackImageStorage && n == encodedImageSize) {
        imageStorage = p;
        ++imageStorageAllocations;
        // Allow result iterator proxies; target the decoded RGBA copy instead.
        failImageCopyStorage = true;
    }
}
bool rejectAllocation(size_t n) {
    if (failImageCopyStorage && n == imageCopyBytes) {
        failImageCopyStorage = false;
        ++allocationFaults;
        return true;
    }
    if (failAfter >= 0 && failAfter-- == 0) { ++allocationFaults; return true; }
    return false;
}
}
void* operator new(std::size_t n) {
    if (rejectAllocation(n)) throw std::bad_alloc();
    auto* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    trackAllocation(p, n);
    return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (rejectAllocation(n)) return nullptr;
    auto* p = std::malloc(n ? n : 1);
    if (p) trackAllocation(p, n);
    return p;
}
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p) noexcept {
    if (p == fontStorage) fontStorage = nullptr;
    if (p == fontAtlas) fontAtlas = nullptr;
    if (p == imageStorage) imageStorage = nullptr;
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

using namespace tc;
namespace {
using namespace std;
#include "../../asyncLoads/src/fontFixture.h"
int failures = 0;
void check(const char* name, bool passed) {
    std::printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
#ifdef __EMSCRIPTEN__
struct Timer { em_arg_callback_func callback; void* data; int id = 0; };
std::vector<Timer> timers;
int nextTimer = 1, abortReentries = 0;
double testNow = 0;
std::unordered_set<internal::BoundedFontFetch*> fetches;
void runTimers() {
    auto batch = std::move(timers); timers.clear();
    for (auto& timer : batch) timer.callback(timer.data);
}
EM_JS(void, installAudioMock, (), {
    globalThis.loadCreditPromises = [];
    globalThis.OfflineAudioContext = function() {};
    OfflineAudioContext.prototype.decodeAudioData = function() {
        var pending = {};
        loadCreditPromises.push(pending);
        return {then: function(resolve) {
            pending.resolve = resolve;
            return {catch: function(reject) { pending.reject = reject; }};
        }};
    };
});
EM_JS(void, resolveAudio, (int index), {
    loadCreditPromises[index].resolve({
        numberOfChannels: 1, sampleRate: 44100, length: 4,
        getChannelData: function() { return new Float32Array([.5, .5, .5, .5]); }
    });
});
EM_JS(int, audioSlots, (), { return Module['trusscAacSlots'] ? Module['trusscAacSlots'].size : 0; });
#endif
#include "../../common/resourceLoadLocalChecks.h"
void localNoticeWithoutAllocation(bool discardSnapshot) {
    struct NoticeState : internal::ResourceLoadState {
        std::unique_ptr<unsigned char[]> payload;
        size_t retainedBytes() const noexcept override { return payload ? 64 : 0; }
        void discardPayload() noexcept override { payload.reset(); }
    };
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    auto payload = std::make_shared<NoticeState>();
    payload->payload = std::make_unique<unsigned char[]>(64);
    payload->localFile = true;
    payload->run = internal::appRunToken();
    std::shared_ptr<internal::ResourceLoadState> state = payload;
    internal::setResourceLoadLimitsForTests(4, 64, 1);
    int notices = 0;
    auto logger = getLogger().onLog.listen([&](LogEventArgs& message) {
        if (message.level == LogLevel::Notice && message.message.find("[ResourceLoad]") != std::string::npos) ++notices;
    });
    const int faultsBefore = allocationFaults;
    failAfter = 0;
    internal::completeResourceLoad(state);
    const bool noAllocation = failAfter == 0 && allocationFaults == faultsBefore;
    failAfter = -1;
    check("local over-budget completion and Notice posting allocate nothing", noAllocation && state->result && notices == 0);
    auto snapshot = internal::takeResourceLoadBatch();
    // Keep the old run's Notice in a snapshot while the next run overflows.
    // Only the current run may log; the old pending Notice is omitted.
    if (discardSnapshot) {
        internal::appRunToken()->store(false);
        internal::waitForResourceLoadsAtAppExit();
    }
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    auto nextPayload = std::make_shared<NoticeState>();
    nextPayload->payload = std::make_unique<unsigned char[]>(64);
    nextPayload->localFile = true;
    nextPayload->run = internal::appRunToken();
    std::shared_ptr<internal::ResourceLoadState> nextState = nextPayload;
    failAfter = 0;
    internal::completeResourceLoad(nextState);
    const bool nextNoAllocation = failAfter == 0 && allocationFaults == faultsBefore;
    failAfter = -1;
    if (discardSnapshot) {
        failAfter = 0;
        snapshot = {};
        const bool discardNoAllocation = failAfter == 0 && allocationFaults == faultsBefore;
        failAfter = -1;
        check("discarding old snapshot reposts current Notice without allocation", discardNoAllocation && notices == 0);
    } else {
        internal::deliverResourceLoadBatch(std::move(snapshot));
    }
    drain();
    check(discardSnapshot ? "discarded old snapshot preserves exactly one current-run Notice and retires credits" :
        "run change replaces stale Notice without allocation and credits retire", nextNoAllocation && notices == 1 &&
        internal::resourceLoadUsageForTests().retainedBytes == 0 && !payload->payload && !nextPayload->payload);
    if (discardSnapshot) {
        auto repeated = std::make_shared<NoticeState>();
        repeated->payload = std::make_unique<unsigned char[]>(64);
        repeated->localFile = true;
        repeated->run = internal::appRunToken();
        internal::completeResourceLoad(repeated);
        drain();
        check("repeated overage after snapshot discard does not duplicate Notice", notices == 1 &&
            internal::resourceLoadUsageForTests().retainedBytes == 0);
    }
    internal::setResourceLoadLimitsForTests(4, 64, size_t(1024) * 1024 * 1024);
}
void localNoticeWithoutAllocation() {
    localNoticeWithoutAllocation(false);
    localNoticeWithoutAllocation(true);
}
void publicationAllocationFailure() {
    // Build the candidate first; fail a cache-publication allocation only
    // when a later pass applies it. Track both retained backing allocations.
    const auto path = fs::absolute("resourceLoadCreditsWeb-publication.ttf");
    const auto bytes = makeFont();
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Font publication;
    int publicationEvents = 0;
    LoadError publicationError = LoadError::None;
    auto publicationListener = publication.onLoad.listen([&](LoadResult& result) {
        ++publicationEvents; publicationError = result.error;
    });
    fontStorageSize = bytes.size();
    trackFontStorage = true;
    publication.loadAsync(path, 16);
#ifdef __EMSCRIPTEN__
    drain();
#else
    internal::waitForResourceLoadsForTests();
#endif
    trackFontStorage = false;
    check("Font candidate owns file storage and CPU atlas before publication",
        fontStorage && fontAtlas && internal::resourceLoadUsageForTests().retainedBytes >= bytes.size() + 65536);
    // Arm at cache insertion, after all key copies, independently of allocator
    // implementation and of how many allocations preparation needed.
    const int faultsBefore = allocationFaults;
    internal::setResourceLoadProbeForTests([](const char* stage, const fs::path&) {
        if (std::strcmp(stage, "font.publish") == 0) failAfter = 0;
    });
    drain();
    failAfter = -1;
    internal::setResourceLoadProbeForTests({});
    check("Font cache publication allocation fault fires and settles once",
        allocationFaults == faultsBefore + 1 && publicationEvents == 1 && publicationError == LoadError::Unknown && !publication.isLoaded());
    check("Font failed publication releases both retained allocations",
        !fontStorage && !fontAtlas && internal::resourceLoadUsageForTests().retainedBytes == 0 && internal::resourceLoadUsageForTests().outstanding == 0);
    drain();
    check("Font publication failure has no duplicate notification", publicationEvents == 1);

    fs::remove(path);
}

void imageCopyAllocationFailure() {
    const auto path = fs::absolute("resourceLoadCreditsWeb-copy.ppm");
    unsigned char bytes[encodedImageSize]{};
    const char header[] = "P6\n8 8\n255\n";
    std::memcpy(bytes, header, sizeof(header) - 1);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    const int faultsBefore = allocationFaults;
    int events = 0;
    LoadError error = LoadError::None;
    {
        Image image;
        auto listener = image.onLoad.listen([&](LoadResult& result) {
            ++events; error = result.error;
        });
        internal::setResourceLoadProbeForTests([](const char* stage, const fs::path&) {
            if (std::strcmp(stage, "image.decode") == 0) trackImageStorage = true;
        });
        image.loadAsync(path);
#ifdef __EMSCRIPTEN__
        drain();
#else
        internal::waitForResourceLoadsForTests();
#endif
        trackImageStorage = false;
        failImageCopyStorage = false;
        failAfter = -1;
        internal::setResourceLoadProbeForTests({});
        drain();
        check("Image copy allocation fault fires after encoded storage exists",
            imageStorageAllocations == 1 && allocationFaults == faultsBefore + 1);
        check("Image copy allocation failure settles once",
            events == 1 && error == LoadError::Unknown && !image.isAllocated());
    }
    check("Image destruction leaves no encoded storage or completion credits",
        !imageStorage && internal::resourceLoadUsageForTests().retainedBytes == 0 &&
        internal::resourceLoadUsageForTests().outstanding == 0);
    drain();
    check("Image copy failure has no duplicate notification", events == 1);
    fs::remove(path);
}

#ifdef __EMSCRIPTEN__
void externalRejections() {
    // Default/unclassified internal input stays external, including on Web.
    struct External {
        Event<LoadResult> event;
        internal::ResourceLoadSlot<internal::ResourceLoadState> slot;
        int events = 0;
        LoadError error = LoadError::None;
        EventListener listener = event.listen([this](LoadResult& result) { ++events; error = result.error; });
        void start() {
            auto state = slot.begin(event, [](auto&) {});
            internal::startExternalResourceLoad(state, [](auto*) {});
        }
    };
    internal::setResourceLoadLimitsForTests(4, 64, 1024 * 1024);
    const auto rejected = internal::resourceLoadUsageForTests().rejected;
    std::vector<std::unique_ptr<External>> requests;
    for (int i = 0; i < 70; ++i) {
        auto request = std::make_unique<External>(); request->start(); requests.push_back(std::move(request));
    }
    check("Web external admission keeps four active / 64 pending and rejects excess",
        internal::resourceLoadUsageForTests().active == 4 && internal::resourceLoadUsageForTests().pending == 64 &&
        internal::resourceLoadUsageForTests().rejected == rejected + 2);
    for (int i = 0; i < 80; ++i) drain();
    int successes = 0, failures = 0;
    for (auto& request : requests) {
        if (request->events == 1 && request->error == LoadError::None) ++successes;
        if (request->events == 1 && request->error == LoadError::TooLarge) ++failures;
    }
    check("Web external requests settle with existing hard rejection policy", successes == 68 && failures == 2);

    Font url;
    int events = 0;
    LoadError error = LoadError::None;
    auto listener = url.onLoad.listen([&](LoadResult& result) { ++events; error = result.error; });
    internal::setResourceLoadLimitsForTests(4, 64, 1);
    url.loadAsync("https://test/over-budget.ttf", 16); drain();
    auto* fetch = *fetches.begin();
    const auto bytes = makeFont();
    fetch->data = static_cast<char*>(std::malloc(bytes.size()));
    std::memcpy(const_cast<char*>(fetch->data), bytes.data(), bytes.size());
    fetch->numBytes = bytes.size();
    fetch->complete(fetch, true);
    check("Web URL result over byte credits is discarded before application",
        internal::resourceLoadUsageForTests().retainedBytes == 0);
    drain();
    check("Web URL completion still rejects TooLarge exactly once", events == 1 && error == LoadError::TooLarge && !url.isLoaded());
    internal::setResourceLoadLimitsForTests(4, 64, size_t(1024) * 1024 * 1024);
}
#endif

}
#ifdef __EMSCRIPTEN__
extern "C" {
void __wrap_emscripten_async_call(em_arg_callback_func callback, void* data, int) { timers.push_back({callback, data}); }
double __wrap_emscripten_get_now() { return testNow; }
int __wrap_emscripten_set_timeout(em_arg_callback_func callback, double, void* data) {
    int id = nextTimer++;
    timers.push_back({callback, data, id});
    return id;
}
void __wrap_emscripten_clear_timeout(int id) { std::erase_if(timers, [id](const auto& timer) { return timer.id == id; }); }
internal::BoundedFontFetch* __wrap__ZN6trussc8internal21startBoundedFontFetchEPKcPvjPFvPNS0_16BoundedFontFetchEbE(
    const char*, void* data, unsigned, void (*complete)(internal::BoundedFontFetch*, bool)) {
    auto* fetch = new internal::BoundedFontFetch;
    fetch->userData = data; fetch->complete = complete;
    fetches.insert(fetch);
    return fetch;
}
void __wrap__ZN6trussc8internal21closeBoundedFontFetchEPNS0_16BoundedFontFetchE(internal::BoundedFontFetch* fetch) {
    if (!fetch) return;
    ++abortReentries;
    fetch->complete(fetch, false); // abort may reenter before close returns
    fetches.erase(fetch);
    std::free(const_cast<char*>(fetch->data));
    delete fetch;
}
}
#endif
TC_CORE_TEST_MAIN() {
#ifndef __EMSCRIPTEN__
    getMainThreadId();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    localFileAdmissionChecks();
    localNoticeWithoutAllocation();
    publicationAllocationFailure();
    imageCopyAllocationFailure();
    return failures ? 1 : 0;
#else
    getMainThreadId();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    externalRejections();
    abortReentries = 0;
    installAudioMock();
    std::ofstream("/credit.m4a") << "mock";
    std::vector<std::unique_ptr<SoundBuffer>> sounds;
    for (int i = 0; i < 5; ++i) sounds.push_back(std::make_unique<SoundBuffer>());
    int events = 0;
    auto listener = sounds[4]->onLoad.listen([&](LoadResult& result) { check("AAC surviving result succeeds", result.ok()); ++events; });
    for (auto& sound : sounds) sound->loadAsync("/credit.m4a");
    check("Web preparations are deferred and bounded before backend work", audioSlots() == 0 && internal::resourceLoadUsageForTests().active == 4 && internal::resourceLoadUsageForTests().pending == 1);
    drain();
    check("four asynchronous browser decodes retain their active slots", audioSlots() == 4 && internal::resourceLoadUsageForTests().active == 4);
    sounds[0]->generateSilence(0);
    check("cancel closes browser slot and admits pending request", audioSlots() == 3 && internal::resourceLoadUsageForTests().pending == 0);
    drain();
    check("replacement browser decode starts only in a later pass", audioSlots() == 4);
    resolveAudio(4); runTimers();
    check("browser completion retains PCM credits until application", events == 0 && internal::resourceLoadUsageForTests().retainedBytes >= 4 * sizeof(float));
    drain();
    check("Web application returns all completed payload credits once", events == 1 && internal::resourceLoadUsageForTests().retainedBytes == 0);
    resolveAudio(0); runTimers(); drain();
    check("late cancelled browser promise cannot return stale credit/data", events == 1 && sounds[0]->samples.empty());
    internal::waitForResourceLoadsAtAppExit();
    check("Web app exit cancels all outstanding backends and active slots", audioSlots() == 0 && internal::resourceLoadUsageForTests().active == 0 && internal::resourceLoadUsageForTests().pending == 0);
    runTimers(); drain();
    check("cancelled backend timers do not dispatch application events", events == 1);

    Font font;
    int fontEvents = 0;
    LoadResult fontResult;
    auto fontListener = font.onLoad.listen([&](LoadResult& result) { ++fontEvents; fontResult = result; });
    font.loadAsync("https://test/cancel.ttf", 16); drain();
    check("Font fetch and deadline share one active preparation", fetches.size() == 1 && timers.size() == 1 && internal::resourceLoadUsageForTests().active == 1);
    font.clearAtlas();
    check("Font cancel closes fetch, removes timer and returns active slot", fetches.empty() && timers.empty() && internal::resourceLoadUsageForTests().active == 0 && fontEvents == 0);
    font.loadAsync("https://test/success.ttf", 16); drain();
    auto* fetch = *fetches.begin();
    auto bytes = makeFont();
    fetch->data = static_cast<char*>(std::malloc(bytes.size()));
    std::memcpy(const_cast<char*>(fetch->data), bytes.data(), bytes.size());
    fetch->numBytes = bytes.size();
    fetch->complete(fetch, true);
    check("Font URL completion retains decoded storage and cancels timer", internal::resourceLoadUsageForTests().retainedBytes >= bytes.size() + 256 * 256 && timers.empty() && fontEvents == 0);
    drain();
    check("Font URL result applies once and returns credits", fontEvents == 1 && fontResult && font.isLoaded() && internal::resourceLoadUsageForTests().retainedBytes == 0);
    font.loadAsync("https://test/timeout.ttf", 16); drain();
    testNow += internal::webLoadTimeoutMSecs;
    runTimers(); drain();
    check("Font timeout fails once and releases active/storage credits", fontEvents == 2 && !fontResult && fetches.empty() && timers.empty() && internal::resourceLoadUsageForTests().active == 0 && internal::resourceLoadUsageForTests().retainedBytes == 0);
    font.loadAsync("https://test/allocation.ttf", 16); drain();
    internal::setResourceLoadProbeForTests([](const char* stage, const fs::path&) {
        if (std::strcmp(stage, "font.url-decode") == 0) throw std::bad_alloc();
    });
    fetch = *fetches.begin();
    fetch->data = static_cast<char*>(std::malloc(bytes.size()));
    std::memcpy(const_cast<char*>(fetch->data), bytes.data(), bytes.size());
    fetch->numBytes = bytes.size();
    fetch->complete(fetch, true);
    internal::setResourceLoadProbeForTests({});
    drain();
    check("Font callback allocation failure settles through host exception guard", fontEvents == 3 && fontResult.error == LoadError::Unknown && fetches.empty() && timers.empty() && internal::resourceLoadUsageForTests().active == 0);
    font.loadAsync("https://test/shutdown.ttf", 16); drain();
    internal::waitForResourceLoadsAtAppExit();
    check("Font shutdown cancels guest timer before unload", fetches.empty() && timers.empty() && internal::resourceLoadUsageForTests().active == 0 && fontEvents == 3 && abortReentries == 5);

    Event<LoadResult> throwingEvent;
    internal::ResourceLoadSlot<internal::ResourceLoadState> throwingSlot;
    int thrownEvents = 0;
    auto thrownListener = throwingEvent.listen([&](LoadResult& result) { check("Web preparation exception becomes failure", result.error == LoadError::Unknown); ++thrownEvents; });
    auto throwing = throwingSlot.begin(throwingEvent, [](internal::ResourceLoadState&) {});
    std::weak_ptr<internal::ResourceLoadState> throwingWeak = throwing;
    internal::startExternalResourceLoad(throwing, [](const auto&) { throw std::bad_alloc(); });
    throwing.reset();
    drain(); drain();
    check("Web failed preparation destroys its State", throwingWeak.expired());
    check("Web preparation failure settles once without app exception catching", thrownEvents == 1 && !throwingSlot.loading() && internal::resourceLoadUsageForTests().active == 0);

    localFileAdmissionChecks();
    localNoticeWithoutAllocation();
    publicationAllocationFailure();
    imageCopyAllocationFailure();

    SoundBuffer failed;
    LoadResult error;
    int errors = 0;
    auto failureListener = failed.onLoad.listen([&](LoadResult& result) { error = result; ++errors; });
    internal::failResourceLoadLaunchForTests(true);
    failed.loadAsync("/credit.m4a");
    internal::failResourceLoadLaunchForTests(false);
    drain(); drain();
    check("Web admission allocation failure settles without exception support", errors == 1 && error.error == LoadError::Unknown && internal::resourceLoadUsageForTests().active == 0);
    return failures ? 1 : 0;
#endif
}
