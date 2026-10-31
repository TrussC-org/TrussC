// Supervised idle ending and busy no-App-exit regressions; never logs out the OS.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cmath>
#include <thread>
using namespace tc;
namespace {
int failures=0, exits=0, cleanups=0, queryCalls=0;
float delay=0;
bool idleCommit=false;
bool logCommit=false;
std::shared_ptr<Window> windows[3];
void check(const char* label,bool good) {
    std::printf("sessionEndBusy: %s: %s\n",label,good?"PASS":"FAIL");
    if (!good) ++failures;
}
struct Probe : App {
    int id;
    explicit Probe(int value):id(value) {}
    void exit() override {
        ++exits;
        logNotice("idle-log") << "App final exit line " << id;
        check("reverse attachment exit exactly once", id==4-exits);
        std::printf("EXIT %d\n",id);
        if (idleCommit) internal::commitSessionEnd(); // dedupe must preserve the current ending
        if (delay>0) std::this_thread::sleep_for(std::chrono::duration<float>(delay));
        std::printf("RETURN %d\n",id);
        if (logCommit && id == 1) std::fputs("App final stdout exit line\n", stdout);
    }
    void cleanup() override { ++cleanups; }
};
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
    logCommit = argc > 1 && std::string(argv[1]) == "idle-log";
    if (logCommit) std::ios::sync_with_stdio(false); // Exercise buffered C stdout separately from Logger's cout.
    if (!logCommit) std::setvbuf(stdout,nullptr,_IONBF,0);
    std::fputs("ENTERED MAIN sessionEndBusy\n",stderr);
    if (argc==1) { deterministic();return failures?1:0; }
    const std::string mode=argv[1];delay=argc>2?std::strtof(argv[2],nullptr):0;
    getMainThreadId();
    if (mode=="watchdog-runs") {
        for (int i=0;i<100;++i) {
            internal::beginRuntimeRun();internal::beginFrameworkLifetime();
            internal::armLogoutWatchdog(3600);internal::cancelLogoutWatchdog();internal::shutdownRuntime();
        }
        internal::beginRuntimeRun();internal::beginFrameworkLifetime();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));internal::shutdownRuntime();
        check("arm/disarm cannot kill a subsequent Runtime",true);return 0;
    }
    internal::beginRuntimeRun();internal::beginFrameworkLifetime();
    headless::active=true;
    if (argc>3) runtime().setLogoutTimeout(std::strtof(argv[3],nullptr));
    if (mode=="negative") runtime().setLogoutTimeout(-1);
    if (mode=="nan") runtime().setLogoutTimeout(NAN);
    if (logCommit) {
        if (argc < 5 || !setLogFile(argv[4])) return 97;
        runtime().setLogoutTimeout(INFINITY);
        setLogLevel(LogLevel::Notice);
    }
    for(int i=0;i<3;++i) {
        auto& w=windows[i];w=std::make_shared<Window>();
        internal::WindowRequestAccess::bindForTests(*w,900+i);
        internal::retainRuntimeWindow(w);w->setApp<Probe>(i+1);
        internal::applyPendingAppForTests(*w);
        internal::currentWindowCtx()=&w->context();w->tickTree();
    }
    internal::currentWindowCtx()=nullptr;
    auto veto=runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) { ++queryCalls;e.cancel=true; });
    if (mode=="query" || mode=="worker-query") {
        std::atomic<bool> entered=false,stop=false;
        std::thread worker;
        if (mode=="worker-query") {
            worker=std::thread([&] { internal::WorkerAppAdmission active({});entered=true;while(!stop)std::this_thread::yield(); });
            while(!entered)std::this_thread::yield();
        }
        check("idle main query retains veto even with worker activity",!internal::querySessionEnd());
        check("query deduplicated",!internal::querySessionEnd() && queryCalls==1);
        internal::cancelSessionEnd();check("cancel permits a new query",!internal::querySessionEnd() && queryCalls==2);
        stop=true;if(worker.joinable())worker.join();
        // The explicit ordinary ending below must not simulate a new OS commit.
        veto = {};
        runtime().setLogoutTimeout(0);
        internal::shutdownRuntime();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check("normal complete cleanup",exits==3 && cleanups==3);
        return failures?1:0;
    }
    if (mode=="busy" || mode=="native-busy" || mode=="constructor") {
        runtime().setLogoutTimeout(0); // any armed watchdog would end this child
        if (mode=="native-busy") internal::setSessionActivityProbe([] { return true; });
        if (mode=="constructor") {
            struct Constructing : App {
                Constructing() {
                    internal::commitSessionEnd();
                    check("busy constructor gets no App exit",exits==0 && cleanups==0);
                }
            };
            auto candidate=internal::makeConstructedApp<Constructing>();
        } else {
            internal::RuntimeActivityScope active;
            check("busy query bypasses veto",internal::querySessionEnd() && queryCalls==0);
            internal::commitSessionEnd();internal::commitSessionEnd();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check("busy commit returns with no App exit or watchdog",exits==0 && cleanups==0);
        return failures?1:0;
    }
    // Inspect the same idle ending seam, separately from terminal children.
    idleCommit=true;
    if (mode=="idle-check") {
        check("idle ending accepted",internal::endRuntimeForSession());
        check("idle common ending includes every cleanup",exits==3 && cleanups==3);
        check("committed ending is deduplicated",!internal::endRuntimeForSession() && exits==3);
        return failures?1:0;
    }
    // Idle commit terminates on this path after complete common ending.
    internal::commitSessionEnd();
    return 96;
}
