#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace std;
using namespace tc;
namespace {
#ifdef __EMSCRIPTEN__
int failures = 0;
unique_ptr<MicInput> first, second;
vector<EventListener> listeners;
vector<ScreenshotSaveEventArgs> shots;
vector<MicStartResult> micResults;
int firstEvents = 0, secondEvents = 0;
internal::ScreenshotQueue queue;
void check(const char* label, bool ok) {
    printf("%-76s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
int action(int operation) {
    switch (operation) {
    case 0:
        getMainThreadId(); internal::appRunToken() = make_shared<atomic<bool>>(true);
        first = make_unique<MicInput>(); second = make_unique<MicInput>();
        listeners.push_back(onScreenshotSave().listen([](ScreenshotSaveEventArgs& args) { shots.push_back(args); }));
        listeners.push_back(first->onStart.listen([](MicStartResult& result) {
            ++firstEvents; micResults.push_back(result);
            check("Web onStart sees settled status on main", !first->isStarting() && first->isRunning() == result.success && isMainThread());
        }));
        listeners.push_back(second->onStart.listen([](MicStartResult& result) {
            ++secondEvents; check("second mic independent readiness", !second->isStarting() && second->isRunning() == result.success);
        }));
        break;
    case 1:
        internal::submitScreenshot(&queue, "directory/same.png");
        internal::submitScreenshot(&queue, "same.png");
        internal::submitScreenshot(&queue, "other.jpg");
        internal::submitScreenshot(&queue, "fourth.png");
        internal::submitScreenshot(&queue, "overflow.png");
        internal::captureScreenshots(queue);
        check("Web capture and cap failure never notify inline", shots.empty() && *queue.admitted == 4);
        drain(); check("Web overflow uses deferred WriteFailed", shots.size() == 1 && shots[0].result.error == SaveError::WriteFailed && shots[0].result.message == "Screenshot queue is full");
        break;
    case 2:
        check("Web URL cleanup still occupies admission", *queue.admitted == 4);
        break;
    case 3:
        drain(); check("independent filename completes first", shots.size() == 2 && shots.back().path == "other.jpg" && shots.back().result.ok());
        break;
    case 4:
        drain(); check("all Web captures retire once", shots.size() == 5 && *queue.admitted == 0);
        break;
    case 5: {
        auto before = shots.size();
        internal::submitScreenshot(&queue, "throw.png"); internal::captureScreenshots(queue);
        check("synchronous Web exception never fires inline", shots.size() == before);
        drain(); check("synchronous Web exception is deferred failure", shots.size() == before + 1 && !shots.back().result);
        break;
    }
    case 6:
        internal::submitScreenshot(&queue, "null.png"); internal::captureScreenshots(queue); break;
    case 7:
        drain(); check("null blob is one failure", shots.size() == 7 && !shots.back().result && *queue.admitted == 0); break;
    case 8:
        internal::submitScreenshot(&queue, "click.png"); internal::captureScreenshots(queue); break;
    case 9:
        drain(); check("click throw is one deferred failure", shots.size() == 8 && !shots.back().result && *queue.admitted == 0); break;
    case 10:
        first->startAsync(44100);
        check("Web publishes starting before permission/resume", first->isStarting() && !first->isRunning() && firstEvents == 0); break;
    case 11:
        check("Web queued readiness keeps starting true", first->isStarting() && firstEvents == 0);
        drain(); check("Web ready event publishes actual sample rate", firstEvents == 1 && first->isRunning() && first->getSampleRate() == 44100); break;
    case 12:
        first->startAsync(); check("Web replacement immediately starts", first->isStarting() && !first->isRunning()); break;
    case 13:
        drain(); check("Web permission denial reports exception name", firstEvents == 2 && !first->isStarting() && !first->isRunning() && micResults.back().message.find("NotAllowedError") != string::npos); break;
    case 14:
        first->startAsync(); first->stop(); check("Web stop cancels unanswered permission", !first->isStarting() && !first->isRunning()); break;
    case 15:
        drain(); check("late cancelled Web permission emits nothing", firstEvents == 2); break;
    case 16:
        first->startAsync(); second->startAsync(); break;
    case 17:
        drain(); check("two Web instances remain independent", first->isStarting() && !first->isRunning() && second->isRunning() && secondEvents == 1);
        first->stop(); second->stop(); break;
    case 18:
        first->startAsync(); break;
    case 19:
        drain(); check("Web resume failure includes retry guidance", firstEvents == 3 && !first->isRunning() && micResults.back().message.find("user gesture") != string::npos); break;
    case 20:
        check("Web legacy bool acknowledges accepted request", first->start() && first->isStarting() && !first->isRunning()); break;
    case 21:
        drain(); check("Web legacy readiness publishes without onStart", first->isRunning() && !first->isStarting() && firstEvents == 3); first->stop(); break;
    case 22:
        first->startAsync(); check("Web missing gesture still defers failure", first->isStarting() && firstEvents == 3); break;
    case 23:
        drain(); check("Web suspended context fails cleanly", firstEvents == 4 && !first->isStarting() && !first->isRunning()); break;
    case 24:
        first->startAsync(); first.reset(); break;
    case 25:
        drain(); check("destroy-before-permission produces no stale event", firstEvents == 8); second.reset(); break;
    case 26:
        first->startAsync(); break;
    case 27:
        drain(); check("Web pending resume fails once with user gesture guidance", firstEvents == 5 &&
            !first->isStarting() && !first->isRunning() && !micResults.back().success &&
            micResults.back().message.find("needs a user gesture") != string::npos); break;
    case 28:
        first->startAsync(); break;
    case 29:
        drain(); check("Web user gesture retry succeeds after pending resume failure", firstEvents == 6 &&
            !first->isStarting() && first->isRunning()); first->stop(); break;
    case 30:
        first->startAsync(); break;
    case 31:
        drain(); check("Web unknown activation waits while resume is pending", firstEvents == 6 && first->isStarting() && !first->isRunning()); break;
    case 32:
        drain(); check("Web unknown activation succeeds when resume resolves", firstEvents == 7 && first->isRunning()); first->stop(); break;
    case 33:
        drain(); check("Web unknown activation succeeds on running statechange", firstEvents == 8 && first->isRunning()); first->stop(); break;
    case 34:
        drain(); check("Web permitted pending resume remains cancellable", firstEvents == 8 && first->isStarting()); first->stop();
        check("Web stop cancels pending resume immediately", !first->isStarting() && !first->isRunning()); break;
    case 35:
        second->startAsync(); break;
    case 36:
        second.reset(); break;
    case 37:
        drain(); check("Web destroy cancels pending resume without stale event", secondEvents == 1); break;
    }
    return failures;
}
void finishTests(int code) {
    emscripten_async_call([](void* value) { emscripten_force_exit(static_cast<int>(reinterpret_cast<intptr_t>(value))); },
                          reinterpret_cast<void*>(static_cast<intptr_t>(code)), 0);
}
using Action = int (*)(int);
using Finish = void (*)(int);
EM_JS_DEPS(mockDeps, "$getWasmTableEntry");
EM_JS(void, runMocks, (Action actionFn, Finish finishFn), {
    const originalSetTimeout = globalThis.setTimeout;
    (async () => {
        const action = number => getWasmTableEntry(actionFn)(number);
        const assert = (message, value) => { if (!value) throw new Error(message); console.log(message + ' PASS'); };
        const deferred = () => {
            let resolve, reject;
            const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
            return {promise, resolve, reject};
        };
        // One event-loop task flushes promise chains; no elapsed-time assertions.
        const turn = () => new Promise(resolve => setImmediate(resolve));
        const realTimeout = originalSetTimeout;
        const timers = [], frames = [], blobs = [], clicks = [], revoked = [], permissions = [], contexts = [];
        const originalFrame = globalThis.requestAnimationFrame;
        globalThis.requestAnimationFrame = callback => { frames.push(callback); return frames.length; };
        const nextFrame = () => { const pending = frames.splice(0); pending.forEach(callback => callback()); };
        let removed = 0, clickThrows = false, suspended = false, processors = 0;
        globalThis.setTimeout = callback => { timers.push(callback); return timers.length; };
        const cleanURLs = () => { while (timers.length) timers.shift()(); };
        Module.canvas = { toBlob(callback, mime) { blobs.push({callback, mime}); } };
        globalThis.document = { querySelector: () => Module.canvas,
            body: { appendChild() {} }, createElement() { return {
                click() { if (clickThrows) throw new Error('click failed'); clicks.push(this.href); },
                remove() { ++removed; }
            }; }
        };
        globalThis.URL = { createObjectURL: blob => 'blob:' + blob.id,
            revokeObjectURL: url => revoked.push(url) };
        Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { userActivation: { isActive: true }, mediaDevices: {
            getUserMedia() { const request = deferred(); permissions.push(request); return request.promise; }
        }}});
        globalThis.window = { AudioContext: function(options) {
            this.sampleRate = options.sampleRate; this.state = 'suspended';
            this.resumed = deferred(); this.closed = 0; contexts.push(this);
            this.resume = () => this.resumed.promise.then(() => { if (!suspended) this.state = 'running'; });
            this.close = () => { ++this.closed; this.state = 'closed'; return Promise.resolve(); };
            this.createMediaStreamSource = () => ({connect() {}, disconnect() {}});
            this.createScriptProcessor = () => { ++processors; return { connect() {}, disconnect() {} }; };
        }};
        const stream = () => { const track = { stopped: 0, stop() { ++this.stopped; } }; return { track, getTracks: () => [track] }; };
        action(0); action(1);
        assert('Web captures are queued in invocation order', blobs.length === 4 && blobs[2].mime === 'image/jpeg');
        blobs[2].callback({id:'other'}); action(2); cleanURLs(); action(3);
        blobs[1].callback({id:'second'});
        assert('reordered same-name blob waits for first capture', clicks.join() === 'blob:other');
        blobs[0].callback({id:'first'}); blobs[3].callback({id:'fourth'});
        assert('same-name downloads preserve capture order', clicks.join() === 'blob:other,blob:first,blob:second,blob:fourth');
        cleanURLs(); action(4);
        assert('download URLs and links are released', revoked.length === 4 && removed === 4);
        const capture = Module.canvas.toBlob;
        Module.canvas.toBlob = () => { throw new Error('tainted canvas'); }; action(5); Module.canvas.toBlob = capture;
        action(6); blobs[4].callback(null); action(7);
        clickThrows = true; action(8); blobs[5].callback({id:'throw'}); cleanURLs(); action(9); clickThrows = false;
        assert('thrown click still releases URL/link', revoked.length === 5 && removed === 5);
        action(10);
        assert('permission and resume begin in initiating stack', permissions.length === 1 && contexts.length === 1);
        navigator.userActivation.isActive = false; // activation expires after the call
        const first = stream(); permissions[0].resolve(first); await turn();
        assert('permission alone creates no processor', processors === 0);
        nextFrame(); await turn(); nextFrame(); await turn();
        assert('activated start survives multiple frames with slow resume', first.track.stopped === 0 && contexts[0].closed === 0 && processors === 0);
        contexts[0].resumed.resolve(); await turn(); nextFrame(); action(11);
        navigator.userActivation.isActive = true;
        action(12); contexts[1].resumed.resolve(); permissions[1].reject({name:'NotAllowedError', message:'permission denied'}); await turn(); action(13);
        assert('replacement stopped first tracks', first.track.stopped === 1);
        action(14); const late = stream(); permissions[2].resolve(late); contexts[2].resumed.resolve(); await turn(); action(15);
        assert('late cancelled permission stops tracks without processor', late.track.stopped === 1 && processors === 1);
        action(16); const other = stream(); contexts[4].resumed.resolve(); permissions[4].resolve(other); await turn(); action(17);
        const cancelled = stream(); permissions[3].resolve(cancelled); contexts[3].resumed.resolve(); await turn();
        assert('cancelled instance does not affect another instance', cancelled.track.stopped === 1 && other.track.stopped === 1);
        action(18); contexts[5].resumed.reject({name:'NotAllowedError', message:'gesture required'}); await turn(); action(19);
        const lateResume = stream(); permissions[5].resolve(lateResume); await turn();
        assert('late permission after resume rejection releases tracks', lateResume.track.stopped === 1);
        action(20); const legacy = stream(); permissions[6].resolve(legacy); contexts[6].resumed.resolve(); await turn(); action(21);
        navigator.userActivation.isActive = false;
        suspended = true; action(22); const blocked = stream(); permissions[7].resolve(blocked); contexts[7].resumed.resolve(); await turn(); action(23); suspended = false;
        assert('suspended context releases tracks', blocked.track.stopped === 1);
        action(26); const pendingResume = stream(); permissions[8].resolve(pendingResume); await turn();
        action(27);
        assert('no activation fails without a frame or settled resume and releases tracks', frames.length === 0 && pendingResume.track.stopped === 1 && contexts[8].closed === 1);
        navigator.userActivation.isActive = true;
        action(28); const retry = stream(); permissions[9].resolve(retry); contexts[9].resumed.resolve(); await turn(); action(29);
        assert('successful retry also retires tracks on stop', retry.track.stopped === 1);
        contexts[8].resumed.resolve(); await turn();
        assert('late resume from failed attempt cannot create a processor', processors === 4 && pendingResume.track.stopped === 1);
        delete navigator.userActivation;
        action(30); const unknown = stream(); permissions[10].resolve(unknown); await turn();
        nextFrame(); await turn(); nextFrame(); action(31);
        assert('missing activation API waits without releasing tracks', unknown.track.stopped === 0 && contexts[10].closed === 0 && processors === 4);
        contexts[10].resumed.resolve(); await turn(); action(32);
        action(30); const stateChanged = stream(); permissions[11].resolve(stateChanged); await turn();
        contexts[11].state = 'running'; contexts[11].onstatechange(); await turn(); action(33);
        assert('running statechange connects before resume settles', processors === 6 && stateChanged.track.stopped === 1);
        contexts[11].resumed.resolve(); await turn();
        assert('late resume after statechange cannot connect twice', processors === 6);
        navigator.userActivation = { isActive: true };
        action(30); const stoppedPending = stream(); permissions[12].resolve(stoppedPending); await turn(); action(34);
        assert('stop releases tracks and listener while activated resume is pending', stoppedPending.track.stopped === 1 && contexts[12].closed === 1 && contexts[12].onstatechange === null);
        contexts[12].resumed.resolve(); await turn();
        action(35); const destroyedPending = stream(); permissions[13].resolve(destroyedPending); await turn(); action(36);
        assert('destroy releases tracks and context while resume is pending', destroyedPending.track.stopped === 1 && contexts[13].closed === 1 && contexts[13].onstatechange === null);
        contexts[13].resumed.resolve(); await turn(); action(37);
        action(24); assert('failure cleanup remains idempotent on replacement', blocked.track.stopped === 1); const destroyed = stream(); permissions[14].resolve(destroyed); contexts[14].resumed.resolve(); await turn(); action(25);
        assert('destroyed mic releases late tracks', destroyed.track.stopped === 1);
        assert('all Web mic keys retired', Module.trusscMicStates.size === 0);
        globalThis.setTimeout = realTimeout;
        globalThis.requestAnimationFrame = originalFrame;
        getWasmTableEntry(finishFn)(action(-1) ? 1 : 0);
    })().catch(error => { globalThis.setTimeout = originalSetTimeout; console.error(error); getWasmTableEntry(finishFn)(1); });
});
#endif
}
TC_CORE_TEST_MAIN() {
#ifdef __EMSCRIPTEN__
    runMocks(action, finishTests);
    emscripten_exit_with_live_runtime();
#else
    printf("SKIP: Web canvas/DOM/permission mocks run under Node\n");
    return 0;
#endif
}
