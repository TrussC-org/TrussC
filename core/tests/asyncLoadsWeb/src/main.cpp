#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <tc/graphics/tcBoundedFontFetch.h>
#endif

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
struct Timer { em_arg_callback_func callback; void* data; int delay; int id = 0; };
vector<Timer> timers;
int nextTimer = 1;
double testNow = 0;
void runTimers() {
    auto batch = std::move(timers); timers.clear();
    for (auto& timer : batch) timer.callback(timer.data);
}

// Exercise the real bounded-fetch adapter with controlled browser promises.
// Abort/cancel are observed immediately, but promises can still settle late.
EM_JS_DEPS(fontMockDeps, "$getWasmTableEntry");
EM_JS(void, installFontMock, (), {
    Module['fontFetchMock'] = [];
    Module['fontCompletionCalls'] = 0;
    // Count calls at the real JS-to-C table boundary, before C can touch a
    // retired request. In these controlled Font scenarios the adapter's done
    // callback is the only table call; forward it unchanged. The successful
    // fetch assertion below also proves this observer sees real completions.
    const realTableEntry = getWasmTableEntry;
    getWasmTableEntry = function(pointer) {
        const callback = realTableEntry(pointer);
        return function(...args) {
            ++Module['fontCompletionCalls'];
            return callback(...args);
        };
    };
    globalThis.fetch = function(url, options) {
        if (Module['fontFetchFailInline']) {
            Module['fontFetchFailInline'] = false;
            throw new Error('mock inline fetch failure');
        }
        return new Promise((resolve, reject) => {
            const entry = {url, resolve, reject, reads: [], readCalls: 0,
                aborts: 0, cancels: 0, bodyCancels: 0, copies: 0};
            // A browser abort can reject while close is still on the stack.
            // The real adapter must quiesce this promise before retiring State.
            options.signal.addEventListener('abort', () => {
                ++entry.aborts;
                if (entry.rejectOnAbort) reject(new Error('mock aborted fetch'));
            });
            Module['fontFetchMock'].push(entry);
        });
    };
});
EM_JS(int, fontCompletions, (), { return Module['fontCompletionCalls']; });
EM_JS(int, fontFetches, (), { return Module['fontFetchMock'].length; });
EM_JS(int, fontRequests, (), { return Module['tcFontStreams'] ? Module['tcFontStreams'].size : 0; });
EM_JS(int, fontAborts, (int index), { return Module['fontFetchMock'][index].aborts; });
EM_JS(void, rejectOnFontAbort, (int index), { Module['fontFetchMock'][index].rejectOnAbort = true; });
EM_JS(int, fontCancels, (int index), { return Module['fontFetchMock'][index].cancels; });
EM_JS(int, fontBodyCancels, (int index), { return Module['fontFetchMock'][index].bodyCancels; });
EM_JS(int, fontReads, (int index), { return Module['fontFetchMock'][index].readCalls; });
EM_JS(int, fontCopies, (int index), { return Module['fontFetchMock'][index].copies; });
EM_JS(void, openFontStreamWithLength, (int index, double length), {
    const entry = Module['fontFetchMock'][index];
    entry.resolve({
        ok: true, status: 200,
        headers: {get: name => name === 'Content-Length' && length >= 0 ? String(length) : null},
        body: {
            cancel: () => { ++entry.bodyCancels; return Promise.resolve(); },
            getReader: () => ({
                read: () => {
                    ++entry.readCalls;
                    return new Promise(resolve => { entry.reads.push(resolve); });
                },
                cancel: () => { ++entry.cancels; return Promise.resolve(); }
            })
        }
    });
});
void openFontStream(int index) { openFontStreamWithLength(index, -1); }
EM_JS(void, fontChunk, (int index, const uint8_t* data, size_t size, bool done), {
    const entry = Module['fontFetchMock'][index];
    const value = data ? new Uint8Array(HEAPU8.subarray(data, data + size)) : new Uint8Array(size);
    // Count copies from this ingress view, so the payload guard is checked
    // independently of the storage guard that would also reject overflow.
    value.subarray = function(begin, end) {
        ++entry.copies;
        return Uint8Array.prototype.subarray.call(this, begin, end);
    };
    const resolve = entry.reads.shift();
    if (resolve) resolve({value, done: !!done});
});
EM_JS(size_t, fontCredits, (internal::BoundedFontFetch* request), {
    return Module['tcFontStreams'].get(request).credits;
});
EM_JS(size_t, fontCapacity, (internal::BoundedFontFetch* request), {
    return Module['tcFontStreams'].get(request).capacity;
});
EM_JS(void, exhaustFontCredits, (internal::BoundedFontFetch* request, size_t limit), {
    const entry = Module['tcFontStreams'].get(request);
    // Existing registry seam: derive the full budget from an observed block
    // reservation, then simulate exhausted credits without oversized payload.
    entry.credits *= Math.ceil(limit / entry.capacity);
});
EM_JS(void, failFontFetch, (int index), {
    Module['fontFetchMock'][index].reject(new Error('mock fetch failure'));
});
EM_JS(void, failNextFontFetch, (), { Module['fontFetchFailInline'] = true; });
// One event-loop turn drains promise continuations, including finally/reader
// cancellation. No assertion depends on elapsed time or a real network.
EM_ASYNC_JS(void, settleFontPromises, (), {
    await new Promise(resolve => setTimeout(resolve, 0));
});
void reply(int index, bool success) {
    if (success) {
        auto bytes = makeFont();
        openFontStream(index); settleFontPromises();
        fontChunk(index, bytes.data(), bytes.size(), true);
    } else {
        failFontFetch(index);
    }
    settleFontPromises();
}

struct FetchResult { int events = 0; bool success = false; size_t bytes = 0; };
internal::BoundedFontFetch* startTestFetch(FetchResult& result) {
    return internal::startBoundedFontFetch("https://test/bounds.ttf", &result,
        internal::webLoadTimeoutMSecs, [](internal::BoundedFontFetch* request, bool success) {
            auto& result = *static_cast<FetchResult*>(request->userData);
            ++result.events; result.success = success; result.bytes = request->numBytes;
            internal::closeBoundedFontFetch(request);
        });
}

void checkFontBounds() {
    const auto limit = internal::fontFetchByteLimit;
    const auto losses = internal::fontFetchLossCountForTests();
    const auto bytes = makeFont();

    int index = fontFetches();
    FetchResult advertised;
    auto* request = startTestFetch(advertised);
    openFontStreamWithLength(index, limit + 1); settleFontPromises();
    check("oversized Content-Length rejects before reading",
        advertised.events == 1 && !advertised.success && advertised.bytes == 0 &&
        fontReads(index) == 0 && fontBodyCancels(index) == 1 && fontRequests() == 0);
    // Also retire a request when a mutation removes the early rejection.
    if (!advertised.events) {
        fontChunk(index, bytes.data(), bytes.size(), true); settleFontPromises();
    }
    check("Content-Length rejection counts one lost fetch",
        internal::fontFetchLossCountForTests() == losses + 1);

    index = fontFetches();
    FetchResult streamed;
    request = startTestFetch(streamed);
    openFontStream(index); settleFontPromises();
    // Several accepted reads stay below the production limit. The next read
    // crosses it and must be rejected before any of that read is copied.
    const auto part = limit / 4;
    for (int i = 0; i < 3; ++i) {
        fontChunk(index, nullptr, part, false); settleFontPromises();
    }
    check("lengthless stream accumulates several reads within limit",
        streamed.events == 0 && fontReads(index) == 4 && fontCapacity(request) >= 3 * part &&
        fontCredits(request) > fontCapacity(request));
    const int copies = fontCopies(index);
    fontChunk(index, nullptr, limit - 3 * part + 1, true); settleFontPromises();
    check("accumulated overflow rejects before copying and cancels reader",
        streamed.events == 1 && !streamed.success && streamed.bytes == 0 &&
        fontCopies(index) == copies && fontCancels(index) == 1 && fontRequests() == 0);
    check("stream overflow counts one lost fetch",
        internal::fontFetchLossCountForTests() == losses + 2);

    index = fontFetches();
    FetchResult storage;
    request = startTestFetch(storage);
    openFontStream(index); settleFontPromises();
    fontChunk(index, nullptr, 1, false); settleFontPromises();
    const auto capacity = fontCapacity(request);
    check("first stream block reserves storage and bookkeeping credits",
        capacity >= 1 && fontCredits(request) > capacity);
    exhaustFontCredits(request, limit);
    fontChunk(index, nullptr, capacity, true); settleFontPromises();
    check("exhausted storage credits reject and cancel reader",
        storage.events == 1 && !storage.success && storage.bytes == 0 &&
        fontCancels(index) == 1 && fontRequests() == 0);
    check("storage rejection counts one lost fetch",
        internal::fontFetchLossCountForTests() == losses + 3);

    index = fontFetches();
    Font split;
    int events = 0;
    bool success = false;
    auto listener = split.onLoad.listen([&](LoadResult& result) { ++events; success = bool(result); });
    split.loadAsync("https://test/split.ttf", 24); drain();
    openFontStreamWithLength(index, bytes.size()); settleFontPromises();
    const auto third = bytes.size() / 3;
    fontChunk(index, bytes.data(), third, false); settleFontPromises();
    fontChunk(index, bytes.data() + third, third, false); settleFontPromises();
    check("split response stays pending until final read", events == 0 && split.isLoading());
    fontChunk(index, bytes.data() + 2 * third, bytes.size() - 2 * third, true);
    settleFontPromises();
    check("split response queues successful delivery", events == 0 && split.isLoading() && fontReads(index) == 3);
    drain();
    check("response within limit loads from several reads",
        events == 1 && success && split.isLoaded() && fontCancels(index) == 1 && fontRequests() == 0);
    check("successful split response does not increment loss count",
        internal::fontFetchLossCountForTests() == losses + 3);
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
int __wrap_emscripten_set_timeout(em_arg_callback_func callback, double delay, void* data) {
    const int id = nextTimer++;
    timers.push_back({callback, data, static_cast<int>(delay), id});
    return id;
}
void __wrap_emscripten_clear_timeout(int id) {
    erase_if(timers, [id](const auto& timer) { return timer.id == id; });
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
    installFontMock();
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

    // Local AAC files use the same four preparation slots, but queue past
    // the external pending limit. Resolve every real AAC slot and retire it.
    {
        const auto rejected = internal::resourceLoadUsageForTests().rejected;
        vector<unique_ptr<SoundBuffer>> queued;
        vector<EventListener> listeners;
        int events = 0, errors = 0;
        int nextAudio = audioPromises();
        for (int i = 0; i < 74; ++i) {
            auto sound = make_unique<SoundBuffer>();
            listeners.push_back(sound->onLoad.listen([&](LoadResult& result) {
                ++events;
                if (!result) ++errors;
            }));
            sound->loadAsync("/one.m4a");
            queued.push_back(std::move(sound));
        }
        auto usage = internal::resourceLoadUsageForTests();
        check("local AAC files queue beyond the network pending limit",
            usage.active == 4 && usage.pending == 70 && usage.outstanding == 74 && usage.rejected == rejected);
        for (int pass = 0; pass < 82 && events < 74; ++pass) {
            drain();
            const int end = audioPromises();
            while (nextAudio < end) resolveAudio(nextAudio++, .5f);
            runTimers(); drain();
        }
        usage = internal::resourceLoadUsageForTests();
        check("queued local AAC files all succeed and retire their credits",
            events == 74 && errors == 0 && audioSlots() == 0 && usage.rejected == rejected &&
            usage.active == 0 && usage.pending == 0 && usage.outstanding == 0 && usage.retainedBytes == 0);
    }

    // Replacement disposes the old request immediately. Late browser promises
    // must not publish into a newer request, even if wasm reuses its address.
    Font font;
    int fontEvents = 0;
    auto fontListener = font.onLoad.listen([&](LoadResult& r) { check("Font URL result succeeds", bool(r)); ++fontEvents; });
    font.loadAsync("https://test/one.ttf", 24);
    check("Font URL fetch never starts inline", fontFetches() == 0 && fontEvents == 0);
    drain();
    check("Font URL uses bounded stream adapter", fontFetches() == 1 && fontRequests() == 1);
    font.loadAsync("https://test/two.ttf", 24);
    check("replacement immediately disposes old Font request", fontRequests() == 0 && fontAborts(0) == 1);
    check("replacement closes cancelled Font stream before starting another", fontRequests() == 0 && fontEvents == 0);
    drain();
    check("Font replacement closes the cancelled backend immediately", fontRequests() == 1 && fontFetches() == 2 && fontAborts(0) == 1);
    const int beforeSuccess = fontCompletions();
    reply(1, true);
    check("successful Font fetch calls the observed C completion once", fontCompletions() == beforeSuccess + 1);
    check("fetch callback queues delivery", fontEvents == 0 && font.isLoading() && fontRequests() == 0);
    drain();
    check("cancelled Font completion cannot replace newer success", fontEvents == 1 && font.isLoaded() && fontRequests() == 0);
    const int beforeReplacementReply = fontCompletions();
    reply(0, true); drain();
    check("replacement prevents late C completion", fontCompletions() == beforeReplacementReply);
    check("cancelled Font promise is safe after newer success", fontEvents == 1 && font.isLoaded() && fontRequests() == 0);

    font.loadAsync("https://test/two.ttf", 24);
    check("URL cache hit never notifies inline", fontEvents == 1);
    drain(); check("URL cache hit deferred once without fetch", fontEvents == 2 && fontFetches() == 2);
    check("URL cache hit deferred once", fontEvents == 2);
    {
        auto doomed = make_unique<Font>();
        int doomedEvents = 0;
        auto doomedListener = doomed->onLoad.listen([&](LoadResult&) { ++doomedEvents; });
        doomed->loadAsync("https://test/destroy.ttf", 24); drain();
        openFontStream(2); settleFontPromises();
        const int beforeDestruction = fontCompletions();
        doomed.reset();
        check("Font destruction immediately cancels stream and request", fontRequests() == 0 && fontAborts(2) == 1 && fontCancels(2) == 1);
        check("Font destruction releases backend and deadline immediately", fontRequests() == 0 && timers.empty());
        drain();
        check("Font destruction closes pending stream without an event", fontRequests() == 0 && doomedEvents == 0 && fontEvents == 2);
        auto bytes = makeFont();
        fontChunk(2, bytes.data(), bytes.size(), true); settleFontPromises(); drain();
        check("Font destruction prevents late C completion", fontCompletions() == beforeDestruction);
        check("late stream read after Font destruction is safe", doomedEvents == 0 && fontRequests() == 0);
    }
    Font moved;
    font.loadAsync("https://test/move.ttf", 24); drain();
    const int beforeMove = fontCompletions();
    moved = std::move(font);
    check("Font move immediately disposes pending request", fontRequests() == 0 && fontAborts(3) == 1);
    check("Font move releases backend and deadline immediately", fontRequests() == 0 && timers.empty());
    drain();
    check("Font move closes pending stream and drops URL event", fontEvents == 2 && !moved.isLoading() && fontRequests() == 0);
    reply(3, true); drain();
    check("Font move prevents late C completion", fontCompletions() == beforeMove);
    check("Font move drops pending URL event", fontEvents == 2 && !moved.isLoading());

    Font expired;
    LoadResult expiredResult;
    int expiredEvents = 0;
    auto expiredListener = expired.onLoad.listen([&](LoadResult& r) { expiredResult = r; ++expiredEvents; });
    expired.loadAsync("https://test/timeout.ttf", 24); drain();
    const int beforeTimeout = fontCompletions();
    testNow += internal::webLoadTimeoutMSecs; runTimers();
    check("Font timeout immediately disposes request before delivery", expiredEvents == 0 && fontRequests() == 0 && fontAborts(4) == 1);
    drain();
    check("Font timeout delivers one deferred error", expiredEvents == 1 && !expiredResult && !expired.isLoaded());
    check("Font timeout closes fetch despite reentrant abort", expiredEvents == 1 && !expired.isLoaded() && fontRequests() == 0 && fontAborts(4) == 1);
    reply(4, true); drain();
    check("Font timeout prevents late C completion", fontCompletions() == beforeTimeout);
    check("late timed-out fetch cannot notify again", expiredEvents == 1 && fontRequests() == 0);

    expired.loadAsync("https://test/fetch-error.ttf", 24); drain();
    reply(5, false);
    check("mocked fetch failure still waits for delivery", expiredEvents == 1 && expired.isLoading() && fontRequests() == 0);
    drain(); check("mocked fetch failure delivered once", expiredEvents == 2 && !expiredResult && !expired.isLoaded() && fontAborts(5) == 1);
    failNextFontFetch();
    expired.loadAsync("https://test/inline-error.ttf", 24); drain();
    check("reentrant fetch failure still waits another pass", expiredEvents == 2 && fontRequests() == 0);
    drain(); check("reentrant fetch failure delivered once", expiredEvents == 3 && !expiredResult && !expired.isLoaded());
    testNow += internal::webLoadTimeoutMSecs; runTimers(); drain();
    check("finished Font timers cannot deliver duplicate results", expiredEvents == 3 && fontEvents == 2 && fontRequests() == 0);
    // Preserve late-success scenarios above, and separately reject from an
    // abort listener while the real close path is still on the stack.
    auto aborting = make_unique<Font>();
    int abortEvents = 0;
    auto abortListener = aborting->onLoad.listen([&](LoadResult&) { ++abortEvents; });
    const int abortIndex = fontFetches();
    aborting->loadAsync("https://test/abort-reject.ttf", 24); drain();
    rejectOnFontAbort(abortIndex);
    const int beforeAbort = fontCompletions();
    aborting.reset();
    check("synchronous abort rejection releases backend and deadline", fontAborts(abortIndex) == 1 && fontRequests() == 0 && timers.empty());
    settleFontPromises(); drain();
    check("abort rejection cannot call C completion after cancellation", fontCompletions() == beforeAbort);
    check("abort rejection cannot notify after cancellation", abortEvents == 0 && fontRequests() == 0);
    int abortReentries = 0;
    for (int index = 0; index < fontFetches(); ++index) abortReentries += fontAborts(index);
    check("all fetch close abort reentries were handled", abortReentries >= 6 && fontRequests() == 0 && timers.empty());
    check("Font cancellation and timeout abort reentries were handled", abortReentries >= 4 && fontRequests() == 0);
    checkFontBounds();
    // Asyncify's initial unwind returns 0 to callMain. Its final rewind does
    // not propagate main's return value to Node, so publish the verdict here.
    emscripten_force_exit(failures ? 1 : 0);
#endif
}
