#include <TrussC.h>
#include "tc/sound/tcMicInputInternal.h"
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace std;
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    printf("%-78s %s\n", label, ok ? "PASS" : "FAIL"); fflush(stdout);
    if (!ok) ++failures;
}
struct Gate {
    mutex guard;
    condition_variable changed;
    int arrivals = 0;
    bool released = false;
    void arrive() { lock_guard<mutex> lock(guard); ++arrivals; changed.notify_all(); }
    void wait(int count = 1) {
        unique_lock<mutex> lock(guard);
        if (!changed.wait_for(lock, chrono::seconds(60), [&] { return arrivals >= count; })) {
            fprintf(stderr, "Gate watchdog: %d/%d arrivals\n", arrivals, count); std::_Exit(5);
        }
    }
    void block() {
        unique_lock<mutex> lock(guard); ++arrivals; changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }
    void open() { lock_guard<mutex> lock(guard); released = true; changed.notify_all(); }
};
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
void finish() { internal::waitForResourceLoadsForTests(); drain(); }
void newRun() {
    if (auto token = internal::appRunToken()) token->store(false);
    internal::cancelResourceBackends();
    internal::appRunToken() = make_shared<atomic<bool>>(true);
}
fs::path sandbox;
#ifndef __EMSCRIPTEN__
void micTests() {
    newRun();
    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::Authorized; }, {});
    MicInput mic;
    int events = 0;
    int startedLogs = 0;
    auto logListener = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.message.find("[MicInput] started (") == 0) {
            ++startedLogs;
            check("async start log has sync format and runs on main", isMainThread() &&
                args.message == "[MicInput] started (44100 Hz, mono, Fake capture)");
        }
    });
    bool expectSuccess = true;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++events;
        check("mic clears isStarting before main-thread event", !mic.isStarting() && isMainThread());
        check("mic publishes running/name before event", result.success == expectSuccess && mic.isRunning() == expectSuccess && (expectSuccess ? mic.getDeviceName() == "Fake capture" : mic.getDeviceName().empty()));
    });
    Gate opening, opened, closing, queued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") queued.arrive();
    });
    thread::id lifecycleId, cleanupId;
    int fakeComBalance = 0;
    internal::setMicBackendForTests([&](internal::MicState& state) {
        lifecycleId = this_thread::get_id(); ++fakeComBalance; opening.block();
        state.name = "Fake capture"; state.sampleRate = 44100;
        float samples[] = {0.25f, 0.5f}; state.audio(samples, 2);
        opened.arrive(); return MicStartResult{true, {}};
    }, [&](internal::MicState&) { cleanupId = this_thread::get_id(); closing.block(); --fakeComBalance; });
    mic.startAsync();
    check("mic is starting immediately after submission", mic.isStarting() && !mic.isRunning() && events == 0);
    opening.wait(); check("mic stays starting while backend is gated", mic.isStarting());
    opening.open(); opened.wait();
    queued.wait();
    check("mic stays starting after readiness is queued", mic.isStarting() && events == 0 && startedLogs == 0);
    drain();
    check("async success delivered once", events == 1 && mic.getSampleRate() == 44100);
    drain();
    check("async success logs exactly once at delivery", startedLogs == 1);
    float samples[2]{};
    check("callback writes retained State ring", mic.getBuffer(samples, 2) == 2 && samples[0] == 0.25f && samples[1] == 0.5f);
    mic.stop(); closing.wait();
    check("stop returns while cleanup is still blocked", !mic.isStarting() && !mic.isRunning() && mic.getDeviceName().empty());
    closing.open(); finish();
    internal::setResourceLoadProbeForTests({});
    check("init and cleanup use one non-main thread", lifecycleId == cleanupId && lifecycleId != this_thread::get_id() && fakeComBalance == 0);
    // Failure is also queued, with actionable backend detail.
    expectSuccess = false;
    internal::setMicBackendForTests([](internal::MicState&) { return MicStartResult{false, "permission denied (backend=-7)"}; }, [](internal::MicState&) {});
    mic.startAsync(); internal::waitForResourceLoadsForTests();
    check("failed startup remains starting until delivery", mic.isStarting() && events == 1);
    auto errorListener = mic.onStart.listen([&](MicStartResult& result) { check("backend detail retained", result.message == "permission denied (backend=-7)"); });
    drain(); check("failed attempt delivers once", events == 2 && !mic.isRunning());
    check("failed async startup emits no started log", startedLogs == 1);
    errorListener.disconnect(); mic.stop();
    Gate stoppedOpen, stoppedCleanup;
    internal::setMicBackendForTests([&](internal::MicState&) {
        stoppedOpen.block(); return MicStartResult{true, {}};
    }, [&](internal::MicState&) { stoppedCleanup.arrive(); });
    mic.startAsync(); stoppedOpen.wait(); mic.stop();
    check("stop returns before blocked startup is released", !mic.isStarting() && !mic.isRunning());
    stoppedOpen.open(); stoppedCleanup.wait(); finish();
    check("stop suppresses late successful startup", events == 2);
    check("cancelled async startup emits no started log", startedLogs == 1);
    logListener.disconnect();
    // Cancellation at a blocked open keeps State alive without blocking main.
    Gate blockedOpen, lateCleanup;
    internal::setMicBackendForTests([&](internal::MicState& state) {
        blockedOpen.block(); state.name = "late"; return MicStartResult{true, {}};
    }, [&](internal::MicState&) { lateCleanup.arrive(); });
    auto dying = make_unique<MicInput>();
    auto staleListener = dying->onStart.listen([&](MicStartResult&) { check("no stale event after destruction", false); });
    dying->startAsync(); blockedOpen.wait(); dying.reset();
    check("destruction returns before blocked startup is released", events == 2);
    blockedOpen.open(); lateCleanup.wait(); finish();
    // Replacement must wait for predecessor teardown, independently of main.
    Gate firstCleanup, replacementOpen;
    atomic<int> openCount{0}, closeCount{0};
    vector<thread::id> starts, stops; mutex identities;
    map<thread::id, int> threadBalances;
    internal::setMicBackendForTests([&](internal::MicState& state) {
        { lock_guard<mutex> lock(identities); starts.push_back(this_thread::get_id()); ++threadBalances[this_thread::get_id()]; }
        if (++openCount == 2) replacementOpen.arrive();
        state.name = "Fake capture"; return MicStartResult{true, {}};
    }, [&](internal::MicState&) {
        { lock_guard<mutex> lock(identities); stops.push_back(this_thread::get_id()); --threadBalances[this_thread::get_id()]; }
        if (++closeCount == 1) firstCleanup.block();
    });
    expectSuccess = true;
    check("synchronous start shares backend and emits no onStart", mic.start(48000) && mic.isRunning() && events == 2);
    mic.startAsync(48000); firstCleanup.wait();
    check("replacement returns before predecessor teardown", mic.isStarting() && openCount == 1);
    firstCleanup.open(); replacementOpen.wait();
    mic.stop(); finish();
    check("same-rate replacement suppresses stale events", events == 2);
    check("sync and async own matching lifecycle threads", starts.size() == 2 && stops.size() == 2 && starts[0] == stops[0] && starts[1] == stops[1]);
    check("fake COM ownership balances per sync/replacement thread", all_of(threadBalances.begin(), threadBalances.end(), [](const auto& entry) { return entry.second == 0; }));
    // A listener may restart and then delete the emitter safely.
    internal::setMicBackendForTests([](internal::MicState& state) { state.name = "Fake capture"; return MicStartResult{true, {}}; }, [](internal::MicState&) {});
    auto reentrant = make_unique<MicInput>(); int restarted = 0;
    auto reentrantListener = reentrant->onStart.listen([&](MicStartResult&) {
        if (++restarted == 1) reentrant->startAsync(); else reentrant.reset();
    });
    Gate reentrantQueued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") reentrantQueued.arrive();
    });
    reentrant->startAsync();
    reentrantQueued.wait(); drain(); reentrantQueued.wait(2); drain();
    check("onStart listener can restart and delete mic", restarted == 2 && !reentrant);
    finish();
    internal::setResourceLoadProbeForTests({});
    mic.startAsync(); newRun(); finish();
    check("run invalidation retires mic and suppresses completion", events == 2 && !mic.isStarting() && !mic.isRunning() && mic.getDeviceName().empty());
    mic.stop();
    // Model partial context/device initialization and every cleanup stage.
    for (int failedStage = 0; failedStage < 3; ++failedStage) {
        thread::id initThread, uninitThread;
        int contexts = 0, devices = 0;
        internal::setMicBackendForTests([&](internal::MicState& state) {
            initThread = this_thread::get_id(); ++contexts;
            if (failedStage == 0) return MicStartResult{false, "context init failure"};
            ++devices;
            if (failedStage == 1) return MicStartResult{false, "device init failure"};
            state.name = "Fake capture"; return MicStartResult{false, "device start failure"};
        }, [&](internal::MicState&) {
            uninitThread = this_thread::get_id(); devices = 0; --contexts;
        });
        mic.startAsync(); internal::waitForResourceLoadsForTests(); mic.stop(); drain();
        check("partial initialization cleanup stays on owning thread", contexts == 0 && devices == 0 && initThread == uninitThread && initThread != this_thread::get_id());
    }
    internal::setMicBackendForTests({}, {});
}

void micAuthorizationTests() {
    newRun();
    using Authorization = internal::MicAuthorization;
    Authorization status = Authorization::Authorized;
    function<void(bool)> answer;
    int requests = 0, events = 0, readyCount = 0;
    atomic<int> opens{0}, closes{0};
    thread::id openedOn, closedOn;
    MicStartResult outcome;
    const string denied = "Microphone access denied (System Settings → Privacy & Security → Microphone)";
    Gate ready;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") ready.arrive();
    });
    internal::setMicAuthorizationForTests([&] { return status; }, [&](function<void(bool)> completion) {
        ++requests; answer = std::move(completion);
    });
    internal::setMicBackendForTests([&](internal::MicState& state) {
        ++opens; openedOn = this_thread::get_id(); state.name = "Authorized capture";
        return MicStartResult{true, {}};
    }, [&](internal::MicState&) { ++closes; closedOn = this_thread::get_id(); });
    MicInput mic;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++events; outcome = result;
        check("permission completion settles status on main", isMainThread() && !mic.isStarting() && mic.isRunning() == result.success);
    });
    int deniedLogs = 0, requestedLogs = 0, waitingLogs = 0;
    bool waitingNoticeOnMain = true;
    auto logListener = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.message == "[MicInput] " + denied) ++deniedLogs;
        if (args.message == "[MicInput] permission requested; call start() again after granting") ++requestedLogs;
        if (args.message == "[MicInput] waiting for microphone permission (System Settings → Privacy & Security → Microphone)") {
            ++waitingLogs;
            waitingNoticeOnMain &= args.level == LogLevel::Notice && isMainThread();
        }
    });
    auto settle = [&] { ready.wait(++readyCount); drain(); };
    mic.startAsync(); settle();
    check("Authorized opens normally without requesting permission", requests == 0 && opens == 1 && events == 1 && outcome.success);
    mic.stop(); finish();
    check("Authorized init and cleanup share the lifecycle thread", opens == closes && openedOn == closedOn && openedOn != this_thread::get_id());

    check("Authorized emits no waiting-for-permission notice", waitingLogs == 0);
    for (auto blocked : {Authorization::Denied, Authorization::Restricted}) {
        status = blocked;
        const int before = events;
        mic.startAsync();
        check("Denied/Restricted never opens or delivers inline", opens == 1 && events == before && mic.isStarting() && internal::resourceLoadWorkersForTests() == 0);
        settle();
        check("Denied/Restricted delivers exact Settings guidance", events == before + 1 && !outcome.success && outcome.message == denied && !mic.isRunning());
        check("Denied/Restricted sync fails with same log and no event", !mic.start() && deniedLogs > 0 && events == before + 1 && !mic.isStarting() && opens == 1);
    }
    check("both denied synchronous starts log exact guidance", deniedLogs == 2 && requests == 0);

    check("Denied/Restricted emit no waiting-for-permission notice", waitingLogs == 0);
    status = Authorization::NotDetermined;
    int before = events;
    mic.startAsync();
    check("NotDetermined logs exact Notice once at submission on main", waitingLogs == 1 && waitingNoticeOnMain);
    finish(); // no response, no worker, no wait
    check("unanswered permission does not repeat its waiting notice", waitingLogs == 1);
    check("unanswered permission stays starting without a worker", mic.isStarting() && requests == 1 && opens == 1 && events == before && internal::resourceLoadWorkersForTests() == 0);
    mic.stop(); answer(true); finish();
    check("stop drops a late permission grant", !mic.isStarting() && !mic.isRunning() && opens == 1 && events == before);
    {
        auto destroyed = make_unique<MicInput>();
        destroyed->startAsync(); destroyed.reset();
    }
    answer(false); finish();
    check("destroy drops a late permission denial without a worker", opens == 1 && events == before && internal::resourceLoadWorkersForTests() == 0);

    mic.startAsync();
    thread response([callback = answer] { callback(true); }); response.join();
    check("grant callback cannot initialize on the platform callback thread", opens == 1 && mic.isStarting() && events == before);
    drain(); settle();
    check("NotDetermined grant uses normal deferred lifecycle startup", opens == 2 && events == before + 1 && outcome.success && mic.isRunning());
    mic.stop(); finish();
    check("granted init and teardown share owning worker", opens == closes && openedOn == closedOn && openedOn != this_thread::get_id());

    before = events;
    mic.startAsync(); answer(false); drain(); settle();
    check("NotDetermined denial reports guidance without init", opens == 2 && events == before + 1 && !outcome.success && outcome.message == denied);
    before = events;
    mic.startAsync(); answer(true); mic.stop(); finish();
    check("cancel drops a grant already queued for main", opens == 2 && events == before);
    mic.startAsync(); auto oldAnswer = answer; newRun(); oldAnswer(true); finish();
    check("app exit drops permission completion from the old run", opens == 2 && events == before && !mic.isStarting());

    for (bool granted : {true, false}) {
        check("sync NotDetermined requests and returns false immediately", !mic.start() && !mic.isStarting() && opens == 2 && events == before);
        answer(granted); finish();
        check("sync permission response never auto-starts or fires onStart", opens == 2 && events == before);
    }
    check("sync NotDetermined logs explicit retry guidance", requestedLogs == 2);
    status = Authorization::Authorized;
    check("sync retry after granting opens normally without onStart", mic.start() && mic.isRunning() && opens == 3 && events == before);
    mic.stop(); finish();

    // A cancelled permission-only state must forward older pending cleanup.
    Gate cleanup, replacementOpened;
    atomic<int> serialOpens{0};
    internal::setMicBackendForTests([&](internal::MicState&) {
        if (++serialOpens == 2) replacementOpened.arrive();
        return MicStartResult{true, {}};
    }, [&](internal::MicState&) { if (serialOpens == 1) cleanup.block(); });
    mic.startAsync(); ready.wait(++readyCount); mic.stop(); cleanup.wait();
    status = Authorization::NotDetermined; mic.startAsync(); auto cancelledAnswer = answer;
    status = Authorization::Authorized; mic.startAsync();
    cancelledAnswer(true); drain();
    check("permission cancellation preserves older teardown before restart", serialOpens == 1);
    cleanup.open(); replacementOpened.wait(); ready.wait(++readyCount);
    mic.stop(); finish();
    check("replacement starts once after predecessor cleanup", serialOpens == 2);
    check("only async NotDetermined submissions log once each", waitingLogs == requests - 2 && waitingNoticeOnMain);
    internal::setResourceLoadProbeForTests({});
    internal::setMicBackendForTests({}, {});
    internal::setMicAuthorizationForTests({}, {});
}

void micPermissionRetryTests() {
    newRun(); finish();
    using Authorization = internal::MicAuthorization;
    Authorization status = Authorization::Denied;
    internal::setMicAuthorizationForTests([&] { return status; }, [](function<void(bool)>) {});
    const size_t baseline = internal::micStatesAliveForTests();
    check("retry regression begins without retained mic states", baseline == 0);
    auto retry = [&](MicInput& mic, Authorization mode, bool pendingCleanup) {
        status = mode;
        size_t peak = 0;
        bool workersBounded = true, statusCorrect = true;
        for (int i = 0; i < 5000; ++i) {
            mic.startAsync(); drain();
            peak = max(peak, internal::micStatesAliveForTests() - baseline);
            workersBounded &= internal::resourceLoadWorkersForTests() == (pendingCleanup ? 1u : 0u);
            statusCorrect &= !mic.isRunning() && mic.isStarting() == (mode == Authorization::NotDetermined);
        }
        printf("RETRY %s pending-cleanup=%d attempts=5000 peak-live-states=%zu\n",
            mode == Authorization::Denied ? "Denied" : "NotDetermined", pendingCleanup, peak);
        check("5000 drained permission retries retain at most two live States", peak <= 2 && workersBounded && statusCorrect);
        mic.stop(); drain();
        check("retry stop retains only an unfinished cleanup dependency",
            internal::micStatesAliveForTests() == baseline + (pendingCleanup ? 1u : 0u));
    };
    {
        MicInput mic;
        retry(mic, Authorization::Denied, false);
        retry(mic, Authorization::NotDetermined, false);
    }
    check("workerless retry destruction leaves no live States", internal::micStatesAliveForTests() == baseline);

    // Exercise the same retries while a real lifecycle's teardown is blocked.
    // Only that dependency may survive; the next authorized open must await it.
    Gate cleanup, replacementOpened;
    atomic<int> opens{0}, closes{0};
    internal::setMicBackendForTests([&](internal::MicState&) {
        if (++opens == 2) replacementOpened.arrive();
        return MicStartResult{true, {}};
    }, [&](internal::MicState&) { if (++closes == 1) cleanup.block(); });
    {
        MicInput mic;
        status = Authorization::Authorized;
        check("retry teardown fixture opens its first device", mic.start());
        mic.stop(); cleanup.wait();
        retry(mic, Authorization::Denied, true);
        retry(mic, Authorization::NotDetermined, true);
        status = Authorization::Authorized;
        mic.startAsync(); drain();
        check("authorized retry preserves blocked teardown dependency", opens == 1);
        cleanup.open(); replacementOpened.wait();
        mic.stop(); finish();
        check("authorized retry opens once after older teardown", opens == 2 && closes == 2);
    }
    finish();
    check("retry teardown and destruction release every State", internal::micStatesAliveForTests() == baseline);
    internal::setMicBackendForTests({}, {});
    internal::setMicAuthorizationForTests({}, {});
}

void nativeAutomaticMicTests() {
    newRun();
    internal::setMicAuthorizationForTests({}, {});
    auto& engine = AudioEngine::getInstance();
    engine.shutdown();
    MicInput mic; int delivered = 0; MicStartResult outcome;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++delivered; outcome = result;
        check("automatic mic settles status before its event", !mic.isStarting() && mic.isRunning() == result.success);
    });
    Gate queued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") queued.arrive();
    });
#ifdef __APPLE__
    if (internal::micAuthorizationPlatform() == internal::MicAuthorization::NotDetermined) {
        mic.startAsync();
        check("real unanswered consent submits without a capture worker", mic.isStarting() && internal::resourceLoadWorkersForTests() == 0);
        mic.stop(); finish(); internal::setResourceLoadProbeForTests({});
        printf("SKIP real automatic capture outcome: microphone consent not determined; seam outcomes tested separately\n");
        return;
    }
#endif
    mic.startAsync(); queued.wait();
    check("automatic mic result is deferred", delivered == 0 && mic.isStarting());
    drain();
    check("automatic microphone never reports Null capture success", delivered == 1 &&
        (!outcome.success || mic.getDeviceName().find("NULL") == string::npos));
    const bool noDevice = !outcome.success;
    if (noDevice) check("no capture device fails async startup with actionable guidance",
        !mic.isRunning() && !mic.isStarting() &&
        ((outcome.message.find("microphone") != string::npos && outcome.message.find("permissions") != string::npos) ||
         outcome.message == "Microphone access denied (System Settings → Privacy & Security → Microphone)"));
    else printf("SKIP no-device precondition: real capture device available\n");
    mic.stop(); finish();
    const bool sync = mic.start();
    check("synchronous automatic microphone never reports Null capture", !sync || mic.getDeviceName().find("NULL") == string::npos);
    if (noDevice) check("no capture device also fails synchronous start without an event",
        !sync && !mic.isRunning() && !mic.isStarting() && delivered == 1);
    mic.stop(); finish(); internal::setResourceLoadProbeForTests({});
}

void nativeNullMicTests() {
    newRun();
    // Explicit Null capture tests lifetime, independent of OS consent/device.
    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::Authorized; }, {});
    AudioSettings settings; settings.backend = AudioBackend::Null;
    auto& engine = AudioEngine::getInstance();
    check("Null playback backend initializes for real capture lifetime", engine.init(settings));
    Gate queued;
    internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
        if (string(stage) == "mic-ready") queued.arrive();
    });
    MicInput mic; int delivered = 0;
    auto listener = mic.onStart.listen([&](MicStartResult& result) {
        ++delivered;
        check("real Null microphone applies metadata before event", result.success && mic.isRunning() && !mic.isStarting() && mic.getSampleRate() == 44100 && !mic.getDeviceName().empty());
    });
    mic.startAsync(44100); queued.wait();
    check("real Null async startup is pending until delivery", mic.isStarting() && delivered == 0);
    drain();
    engine.shutdown();
    check("private real capture context survives engine shutdown", mic.isRunning());
    mic.stop(); finish();
    internal::setResourceLoadProbeForTests({});
    check("Null backend reselected before synchronous parity", engine.init(settings));
    check("real Null sync startup shares rate/backend without event", mic.start(48000) && mic.getSampleRate() == 48000 && delivered == 1);
    mic.stop(); finish();
    check("real miniaudio callbacks retire before State release", !mic.isRunning() && internal::resourceLoadWorkersForTests() == 0 && delivered == 1);
    engine.shutdown();
}

Gate headlessOpen;
int headlessMicEvents = 0;
class HeadlessMicApp : public App {
    MicInput mic;
    EventListener listener;
public:
    void setup() override {
        listener = mic.onStart.listen([](MicStartResult&) { ++headlessMicEvents; });
        mic.startAsync(); headlessOpen.wait();
    }
    void update() override { exitApp(); }
    void exit() override {
        check("headless exit invalidates mic readiness before app exit", !mic.isStarting() && !mic.isRunning());
        headlessOpen.open();
    }
};
void headlessTests() {
    thread::id initThread, cleanupThread;
    bool retired = false;
    internal::setMicBackendForTests([&](internal::MicState&) {
        initThread = this_thread::get_id(); headlessOpen.block(); return MicStartResult{true, {}};
    }, [&](internal::MicState&) { cleanupThread = this_thread::get_id(); retired = true; });
    const int result = runHeadlessApp<HeadlessMicApp>();
    check("headless runtime waits for owning-thread mic cleanup", result == 0 && retired && initThread == cleanupThread && headlessMicEvents == 0);
    internal::setMicBackendForTests({}, {});
}

string shellQuote(const string& value) {
#ifdef _WIN32
    return "\"" + value + "\"";
#else
    string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : string(1, c);
    return result + "'";
#endif
}
int exitChild() {
    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::Authorized; }, {});
    // Register proof before the resource host, so it runs after retirement.
    static atomic<int>* retired = new atomic<int>(0);
    static atomic<int>* comBalance = new atomic<int>(0);
    atexit([] { if (retired->load() != 1 || comBalance->load() != 0) std::_Exit(8); });
    newRun();
    static Gate* micGate = new Gate;
    static thread::id ownerThread;
    internal::setMicBackendForTests([](internal::MicState&) {
        ownerThread = this_thread::get_id(); ++*comBalance; micGate->block(); return MicStartResult{true, {}};
    }, [](internal::MicState&) {
        if (ownerThread != this_thread::get_id()) std::_Exit(9);
        --*comBalance; ++*retired;
    });
    auto* mic = new MicInput; mic->startAsync();
    micGate->wait();
    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::NotDetermined; },
        [](function<void(bool)> completion) {
            // Model the OS retaining a request whose user never responds.
            static auto* unanswered = new function<void(bool)>;
            *unanswered = std::move(completion);
        });
    static auto* pendingConsent = new MicInput;
    const auto workersBeforeConsent = internal::resourceLoadWorkersForTests();
    pendingConsent->startAsync();
    check("exit child has unanswered consent without a lifecycle worker", pendingConsent->isStarting() &&
        internal::resourceLoadWorkersForTests() == workersBeforeConsent);
    if (failures) std::_Exit(10);
    atexit([] { micGate->open(); });
    std::exit(0);
}
void exitTests(const char* executable) {
    string command = shellQuote(fs::absolute(executable).string());
#ifdef TC_CORE_TEST_NAME
    command += " " TC_CORE_TEST_NAME;
#endif
    for (auto mode : {"main"}) {
        fflush(nullptr);
        int code = system((command + " --exit-child " + mode).c_str());
#ifndef _WIN32
        code = WIFEXITED(code) ? WEXITSTATUS(code) : 128 + WTERMSIG(code);
#endif
        check("process exit waits for gated mic retirement", code == 0);
    }
}

#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
    (void)argc; (void)argv; printf("SKIP native asyncScreenshotMic; Web mocks are in asyncScreenshotMicWeb\n"); return 0;
#else
    getMainThreadId(); sandbox = fs::absolute("asyncScreenshotMic-fixtures"); fs::create_directories(sandbox); setDataPathRoot(sandbox);
    if (argc == 3 && string(argv[1]) == "--exit-child") return exitChild();
    micTests(); micAuthorizationTests(); micPermissionRetryTests(); nativeAutomaticMicTests(); nativeNullMicTests(); headlessTests(); exitTests(argv[0]);
    fs::remove_all(sandbox);
    return failures ? 1 : 0;
#endif
}
