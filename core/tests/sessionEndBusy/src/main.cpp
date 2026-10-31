// Runtime session policy and deadline arbitration. Terminal cases are supervised
// by test.py; no real desktop logout is initiated by this test.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <thread>
using namespace tc;
namespace {
int failures=0, exits=0, cleanups=0, destructors=0, queryCalls=0, constructors=0;
int order[8]{};
std::shared_ptr<Window> windows[3];
std::atomic<bool> audioEntered{false};
std::atomic<int> frozenAudioCalls{0}, liveAudioCalls{0};
Event<void> appSignal;
std::atomic<int> frozenEventCalls{0};
std::atomic<bool> trigger{false};
bool probeFrozen=false;
float delay=0;
int logCalls=0, logsBeforeCommit=0;
std::atomic<bool> ownerLocked{false};
const char* mode="";
void check(const char* label,bool good) {
    std::printf("sessionEndBusy: %s: %s\n",label,good?"PASS":"FAIL");
    if (!good) ++failures;
}
struct Rejected : App { Rejected() { ++constructors; } void exit() override { std::puts("unpublished App exit: FAIL");std::_Exit(91); } };
void commit() { internal::commitSessionEnd(); std::_Exit(96); }
struct Probe : App {
    int id;
    EventListener render, signal, exitSignal;
    explicit Probe(int value):id(value) { ++constructors; }
    ~Probe() { ++destructors; }
    void setup() override {
        signal=appSignal.listen([] { ++frozenEventCalls; });
        exitSignal=events().exit.listen([] { if (internal::sessionEndExit()) ++frozenEventCalls; });
        render=events().onRender.listen([] { if (trigger && std::string(mode)=="onRender") commit(); });
    }
    void update() override { if (trigger && std::string(mode)=="update") commit(); }
    void draw() override { if (trigger && std::string(mode)=="draw") commit(); }
    void keyPressed(const KeyEventArgs&) override { if (trigger) commit(); }
    void audioOut(AudioOutBuffer&) override {
        if (std::string(mode)=="audio-freeze") {
            if (internal::sessionEndExit()) ++frozenAudioCalls;
            else ++liveAudioCalls;
            return;
        }
        if (!trigger) return;
        audioEntered=true;
        // Deliberately remains in flight until terminal process exit.
        for (;;) std::this_thread::yield();
    }
    void exit() override {
        if (trigger && (std::string(mode)=="cohort" || std::string(mode)=="during-exit" || std::string(mode)=="swap") && !internal::sessionEndExit()) {
            order[exits++]=id;std::printf("EXIT %d\n",id);commit();
        }
        order[exits++]=id;
        std::printf("EXIT %d\n",id);
        if (probeFrozen) {
            appSignal.notify();
            windows[id-1]->events().exit.notify();
            if (id==3) {
                const int beforeCrossNotify=exits;
                windows[0]->events().exit.notify();
                std::thread worker([] { windows[0]->events().exit.notify(); });
                worker.join();
                check("cross-notify cannot advance App 1",exits==beforeCrossNotify);
            }
            check("App-owned ordinary and own exit events frozen",frozenEventCalls==0);
            if (id==3 && std::string(mode)=="audio-freeze") {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                check("new audio callbacks frozen before older Apps end",frozenAudioCalls==0);
            }
            check("no logger reentry",logCalls==logsBeforeCommit);
            check("reverse attachment order",id==(std::string(mode)=="swap"?5-exits:4-exits));
            check("no veto/cleanup/destructor",queryCalls==0 && cleanups==0 && destructors==0);
            check("own Window unavailable",getWindow()==nullptr);
            const int before=constructors;
            windows[0]->setApp<Rejected>();
            windows[1]->close(); windows[2]->destroyApp();
            check("create refused",!createWindow());
            check("constructor admission frozen",constructors==before);
            int calls=0;
            runOnMainThread([&] { ++calls; });
            internal::drainMainThreadQueue();
            check("queue frozen",calls==0);
            check("capture frozen",!internal::queueScreenshot("session.png"));
            internal::WindowEntryScope nested(*windows[0]);
            check("event/frame entry frozen",!nested);
            Texture texture; texture.allocate(2,2);
            Fbo fbo; fbo.allocate(2,2); fbo.begin(); fbo.end();
            check("GPU allocation frozen",!texture.isAllocated() && !fbo.isAllocated());
            auto& c=windows[0]->context();
            check("swapchain acquisition frozen",!c.acquireSwapchain || c.acquireSwapchain(c.acquireSwapchainUser).width==0);
            internal::endRuntimeForSession();
            internal::commitSessionEnd(); // actual native entry must be inert on repetition
            check("repeated commit returns without ending the snapshot",true);
            const auto epoch=internal::runtimeEpoch();bool refused=false;
            try { internal::beginRuntimeRun(); } catch (const std::logic_error&) { refused=true; }
            check("terminal session cannot start another Runtime",refused && epoch==internal::runtimeEpoch() && internal::sessionEndExit());
        }
        if (delay>0) std::this_thread::sleep_for(std::chrono::duration<float>(delay));
        std::printf("RETURN %d\n",id);
        if (failures) std::_Exit(91);
        if (std::string(mode)=="throw") throw std::runtime_error("exit fixture");
    }
    void cleanup() override { ++cleanups; }
};
struct Interrupted : App { Interrupted() { commit(); } };
void deterministic() {
    using D=internal::LogoutDeadline;using C=D::Clock;
    const auto t=C::time_point{};
    D budget;budget.arm(3,t);
    check("deadline not per App (first)",!budget.expire(t+std::chrono::seconds(1)));
    check("deadline not per App (second)",!budget.expire(t+std::chrono::seconds(2)));
    budget.arm(100,t+std::chrono::seconds(2)); // repeat cannot restart the budget
    check("deadline exact",budget.expire(t+std::chrono::seconds(3)));
    check("deadline once",!budget.expire(t+std::chrono::seconds(4)));
    D zero;zero.arm(0,t);check("zero immediate",zero.expire(t));
    D infinity;infinity.arm(INFINITY,t);check("infinite budget",!infinity.expire(C::time_point::max()));
    for(int n=0;n<100;++n) {
        D race;race.arm(0,t);std::thread a([&] { race.expire(t); });race.cancel();a.join();
        check("cancelled worker cannot expire a later run",!race.expire(C::time_point::max()));
    }
}
}
#if defined(__APPLE__) && TARGET_OS_OSX
int runSessionEndDeviceProbe(int,char**);
#endif
TC_CORE_TEST_MAIN(int argc,char** argv) {
#if defined(__APPLE__) && TARGET_OS_OSX
    if (argc>1 && std::string(argv[1])=="--mac-device") return runSessionEndDeviceProbe(argc-1,argv+1);
#endif
    std::setvbuf(stdout,nullptr,_IONBF,0);
    std::fputs("ENTERED MAIN sessionEndBusy\n",stderr);
    if (argc==1) { deterministic();return failures?1:0; }
    mode=argv[1];delay=argc>2?std::strtof(argv[2],nullptr):0;
    getMainThreadId();
    if (std::string(mode)=="watchdog-runs") {
        for (int i=0;i<100;++i) {
            internal::beginRuntimeRun();internal::beginFrameworkLifetime();
            internal::armLogoutWatchdog(3600);
            internal::cancelLogoutWatchdog();
            internal::shutdownRuntime();
        }
        internal::beginRuntimeRun();internal::beginFrameworkLifetime();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        internal::shutdownRuntime();
        check("arm/disarm cannot kill a subsequent Runtime",true);return 0;
    }
    internal::beginRuntimeRun();internal::beginFrameworkLifetime();
    headless::active=true;
    if (argc>3) runtime().setLogoutTimeout(std::strtof(argv[3],nullptr));
    if (std::string(mode)=="negative") runtime().setLogoutTimeout(-1);
    if (std::string(mode)=="nan") runtime().setLogoutTimeout(NAN);
    if ((std::string(mode)=="audio" || std::string(mode)=="audio-idle" || std::string(mode)=="audio-freeze")) {
        AudioSettings settings;settings.backend=AudioBackend::Null;
        if (!AudioEngine::getInstance().init(settings)) return 92;
    }
    for(int i=0;i<3;++i) {
        auto& w=windows[i];w=std::make_shared<Window>();
        internal::WindowRequestAccess::bindForTests(*w,900+i);
        internal::retainRuntimeWindow(w);w->setApp<Probe>(i+1);
        internal::applyPendingAppForTests(*w);
        internal::currentWindowCtx()=&w->context();
        w->tickTree();
    }
    internal::currentWindowCtx()=nullptr;
    auto veto=runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) { ++queryCalls;e.cancel=true; });
    if (std::string(mode)=="late-disposal") {
        internal::shutdownRuntime();internal::endFrameworkLifetime();
        // AppKit may deliver the remaining native closes in any order after
        // terminate has shut sg/sgl down. Every Window is already a closed shell.
        for (int i : {1,2,0,2,1}) {
            internal::windowClosed({uint32_t(900+i)},windows[i].get());
            internal::WindowRequestAccess::teardown(*windows[i]);
        }
        check("late closes after device shutdown are inert",exits==3 && cleanups==3);
        return failures?1:0;
    }
    if (std::string(mode)=="query") {
        check("query veto",!internal::querySessionEnd());
        check("query deduplicated",!internal::querySessionEnd() && queryCalls==1);
        internal::cancelSessionEnd();check("cancel permits a new query",!internal::querySessionEnd() && queryCalls==2);
        internal::shutdownRuntime();check("normal complete cleanup",exits==3 && cleanups==3);
        return failures?1:0;
    }
    auto logs=getLogger().onLog.listen([](LogEventArgs&) { ++logCalls; });
    logsBeforeCommit=logCalls;
    probeFrozen=true;trigger=true;
    if (std::string(mode)=="normal") {
        probeFrozen=false;
        internal::shutdownRuntime();
        check("ordinary quit has full cleanup",exits==3 && cleanups==3);
        return failures?1:0;
    }
    if (std::string(mode)=="idle") { probeFrozen=false;commit(); }
    EventListener beforeExit;
    if (std::string(mode)=="before-exit")
        beforeExit=windows[2]->events().exit.listen([] { commit(); },EventPriority::BeforeApp);
    if (std::string(mode)=="audio-idle") {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while (!audioEntered) { if(std::chrono::steady_clock::now()>deadline)return 94;std::this_thread::yield(); }
        check("worker-only query bypasses veto",internal::querySessionEnd() && internal::querySessionEnd() && queryCalls==0);
        check("worker-only query bypasses veto",internal::querySessionEnd() && internal::querySessionEnd() && queryCalls==0);
        commit();
    }
    if (std::string(mode)=="audio-freeze") {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while (!liveAudioCalls) { if(std::chrono::steady_clock::now()>deadline)return 94;std::this_thread::yield(); }
    }
    if (std::string(mode)=="native-busy") {
        internal::setSessionActivityProbe([] { return true; });
        check("native constructor query bypasses veto",internal::querySessionEnd() && queryCalls==0);
        commit();
    }
    internal::RuntimeActivityScope interrupted;
    check("busy queries bypass veto",internal::querySessionEnd() && internal::querySessionEnd() && queryCalls==0);
    internal::cancelSessionEnd();
    if (std::string(mode)=="swap") {
        windows[0]->setApp<Rejected>();internal::applyPendingAppForTests(*windows[0]);return 98;
    }
    if (std::string(mode)=="cohort") {
        const auto generation=reinterpret_cast<void*>(7);
        internal::appOwner(*windows[0]->getApp())->generation=generation;
        internal::appOwner(*windows[2]->getApp())->generation=generation;
        internal::retireAppGeneration(generation);return 96;
    }
    if (std::string(mode)=="before-exit" || std::string(mode)=="during-exit") { internal::shutdownRuntime();return 97; }
    if (std::string(mode)=="constructor") { windows[0]->setApp<Interrupted>();return 93; }
    if (std::string(mode)=="audio") {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while (!audioEntered) { if(std::chrono::steady_clock::now()>deadline)return 94;std::this_thread::yield(); }
    }
    std::thread ownerLock;
    if (std::string(mode)=="owner-lock") {
        auto owner=internal::appOwner(*windows[2]->getApp());
        ownerLock=std::thread([owner] { std::lock_guard lock(owner->cancellationsMutex);ownerLocked=true;for (;;) std::this_thread::yield(); });
        while (!ownerLocked) std::this_thread::yield();
    }
    auto& w=*windows[0];internal::WindowEntryScope entry(w);
    if (std::string(mode)=="update") w.events().update.notify();
    else if (std::string(mode)=="draw") w.getApp()->draw();
    else if (std::string(mode)=="onRender") w.events().onRender.notify();
    else if (std::string(mode)=="event") {
        sapp_event e{};e.type=SAPP_EVENTTYPE_KEY_DOWN;e.key_code=SAPP_KEYCODE_A;
        internal::dispatchWindowEventForTests(&e,w);
    } else commit();
    return 95; // a committed OS callback never resumes its interrupted caller
}
