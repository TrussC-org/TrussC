#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
#include <map>

using namespace std;
using namespace tc;
namespace {
#ifdef __EMSCRIPTEN__
#include "../../asyncLoads/src/fontFixture.h"
int failures = 0;
void check(const char* name, bool ok) {
    printf("%-75s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
struct Timer { em_arg_callback_func callback; void* data; int delay; };
vector<Timer> timers;
map<emscripten_fetch_t*, emscripten_fetch_attr_t> fetches;
double testNow = 0;
int abortReentries = 0;
bool failInline = false;
void runTimers() {
    auto batch = std::move(timers); timers.clear();
    for (auto& timer : batch) timer.callback(timer.data);
}
void reply(emscripten_fetch_t* fetch, bool success) {
    auto attr = fetches.at(fetch);
    if (success) {
        auto bytes = makeFont();
        auto* data = static_cast<char*>(malloc(bytes.size()));
        memcpy(data, bytes.data(), bytes.size());
        fetch->data = data;
        fetch->numBytes = bytes.size();
        attr.onsuccess(fetch);
    } else {
        fetch->status = 404;
        attr.onerror(fetch);
    }
}

// The real EM_JS AAC functions use this controllable decodeAudioData backend.
// Promise callbacks settle in any explicit order; no sleeps or real codecs.
EM_JS(void, installAudioMock, (), {
    globalThis.trusscAudioPromises = [];
    globalThis.OfflineAudioContext = function() {};
    globalThis.OfflineAudioContext.prototype.decodeAudioData = function() {
        var pending = {};
        trusscAudioPromises.push(pending);
        return {then: function(resolve) {
            pending.resolve = resolve;
            return {catch: function(reject) { pending.reject = reject; }};
        }};
    };
});
EM_JS(void, resolveAudio, (int index, float value), {
    trusscAudioPromises[index].resolve({
        numberOfChannels: 1, sampleRate: 44100, length: 4,
        getChannelData: function() { return new Float32Array([value, value, value, value]); }
    });
});
EM_JS(void, wrapAudioSlotIds, (), { Module['trusscAacNext'] = 0x7ffffffe; });
EM_JS(int, audioSlots, (), { return Module['trusscAacSlots'] ? Module['trusscAacSlots'].size : 0; });
EM_JS(int, audioPromises, (), { return trusscAudioPromises.length; });
#endif
}

#ifdef __EMSCRIPTEN__
extern "C" {
void __wrap_emscripten_async_call(em_arg_callback_func callback, void* data, int delay) {
    timers.push_back({callback, data, delay});
}
double __wrap_emscripten_get_now() { return testNow; }
emscripten_fetch_t* __wrap_emscripten_fetch(emscripten_fetch_attr_t* attr, const char*) {
    auto* fetch = static_cast<emscripten_fetch_t*>(calloc(1, sizeof(emscripten_fetch_t)));
    fetch->userData = attr->userData;
    fetches.emplace(fetch, *attr);
    if (failInline) reply(fetch, false);
    return fetch;
}
int __wrap_emscripten_fetch_close(emscripten_fetch_t* fetch) {
    auto attr = fetches.at(fetch);
    // An abort can call onerror before emscripten_fetch_close returns.
    ++abortReentries;
    attr.onerror(fetch);
    fetches.erase(fetch);
    free(const_cast<char*>(fetch->data));
    free(fetch);
    return 0;
}
}
#endif

TC_CORE_TEST_MAIN() {
#ifndef __EMSCRIPTEN__
    printf("SKIP: asyncLoadsWeb runs with emscripten and mock browser backends\n");
    return 0;
#else
    getMainThreadId();
    internal::appRunToken() = make_shared<atomic<bool>>(true);
    installAudioMock();
    ofstream("/one.m4a") << "mock";
    ofstream("/two.m4a") << "mock";
    SoundBuffer first, second;
    int firstEvents = 0, secondEvents = 0;
    auto firstListener = first.onLoad.listen([&](LoadResult& r) { check("first AAC result succeeds", bool(r)); ++firstEvents; });
    auto secondListener = second.onLoad.listen([&](LoadResult& r) { check("second AAC result succeeds", bool(r)); ++secondEvents; });
    first.loadAsync("/one.m4a"); second.loadAsync("/two.m4a");
    check("Web submission returns without VFS/AudioContext preparation", audioPromises() == 0 && first.isLoading() && second.isLoading());
    drain();
    check("concurrent AAC decodes own independent slots", audioSlots() == 2 && audioPromises() == 2);
    resolveAudio(1, .75f); runTimers();
    check("reverse completion stays deferred", firstEvents == 0 && secondEvents == 0);
    drain(); check("second completes independently", secondEvents == 1 && firstEvents == 0 && second.samples[0] == .75f);
    resolveAudio(0, .25f); runTimers(); drain();
    check("first keeps its own PCM", firstEvents == 1 && first.samples[0] == .25f && audioSlots() == 0);

    first.loadAsync("/one.m4a"); drain();
    first.loadAsync("/two.m4a"); drain();
    resolveAudio(3, .5f); runTimers(); drain();
    resolveAudio(2, .9f); runTimers(); drain();
    check("late cancelled AAC cannot overwrite replacement", firstEvents == 2 && first.samples[0] == .5f && audioSlots() == 0);

    // A timed-out promise is removed, then a new load starts; settling the
    // old promise must not publish into the new run's slot.
    SoundBuffer timeout;
    LoadResult timeoutResult;
    int timeoutEvents = 0;
    auto timeoutListener = timeout.onLoad.listen([&](LoadResult& r) { timeoutResult = r; ++timeoutEvents; });
    timeout.loadAsync("/one.m4a"); drain();
    testNow += internal::webLoadTimeoutMSecs; runTimers(); drain();
    check("AAC timeout has one deferred error and releases slot", timeoutEvents == 1 && !timeoutResult && audioSlots() == 0);
    timeout.loadAsync("/two.m4a"); drain();
    resolveAudio(4, .9f); resolveAudio(5, .125f); runTimers(); drain();
    check("late timed-out promise does not overwrite new decode", timeoutEvents == 2 && timeoutResult && timeout.samples[0] == .125f);

    wrapAudioSlotIds();
    first.loadAsync("/one.m4a"); second.loadAsync("/two.m4a"); drain();
    resolveAudio(7, .875f); resolveAudio(6, .625f); runTimers(); drain();
    check("AAC slot IDs recycle safely at the wasm i32 boundary", first.samples[0] == .625f && second.samples[0] == .875f && audioSlots() == 0);

    // URL requests retain heap States even after move/destruction. Fetch
    // close reenters onerror; each State must still complete only once.
    Font font;
    int fontEvents = 0;
    auto fontListener = font.onLoad.listen([&](LoadResult& r) { check("Font URL result succeeds", bool(r)); ++fontEvents; });
    font.loadAsync("https://test/one.ttf", 24);
    check("Font URL fetch never starts inline", fetches.empty() && fontEvents == 0);
    drain(); auto* oldFetch = fetches.begin()->first;
    font.loadAsync("https://test/two.ttf", 24); drain();
    auto* currentFetch = fetches.begin()->first == oldFetch ? fetches.rbegin()->first : fetches.begin()->first;
    reply(currentFetch, true);
    check("fetch callback queues delivery", fontEvents == 0 && font.isLoading());
    drain(); reply(oldFetch, true); drain();
    check("cancelled Font callback is safe after newer success", fontEvents == 1 && font.isLoaded() && fetches.empty());

    font.loadAsync("https://test/two.ttf", 24);
    check("URL cache hit never notifies inline", fontEvents == 1);
    drain(); check("URL cache hit deferred once", fontEvents == 2);
    {
        auto doomed = make_unique<Font>();
        doomed->loadAsync("https://test/destroy.ttf", 24); drain();
        auto* fetch = fetches.begin()->first;
        doomed.reset(); reply(fetch, true); drain();
        check("Font callback owns state after emitter destruction", fetches.empty());
    }
    Font moved;
    font.loadAsync("https://test/move.ttf", 24); drain();
    auto* movedFetch = fetches.begin()->first;
    moved = std::move(font); reply(movedFetch, true); drain();
    check("Font move drops pending URL event", fontEvents == 2 && !moved.isLoading());

    Font expired;
    int expiredEvents = 0;
    auto expiredListener = expired.onLoad.listen([&](LoadResult&) { ++expiredEvents; });
    expired.loadAsync("https://test/timeout.ttf", 24); drain();
    testNow += internal::webLoadTimeoutMSecs; runTimers(); drain();
    check("Font timeout closes fetch despite reentrant abort", expiredEvents == 1 && !expired.isLoaded() && fetches.empty());
    failInline = true;
    expired.loadAsync("https://test/inline-error.ttf", 24); drain();
    check("reentrant fetch failure still waits another pass", expiredEvents == 1);
    drain(); check("reentrant fetch failure delivered once", expiredEvents == 2 && fetches.empty());
    testNow += internal::webLoadTimeoutMSecs; runTimers(); drain();
    check("all fetch close abort reentries were handled", abortReentries >= 6 && fetches.empty());
    return failures ? 1 : 0;
#endif
}
