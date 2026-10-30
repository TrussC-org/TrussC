#include <TrussC.h>
#include <thread>
#include <cstdio>
using namespace tc;
void reset() {
    headless::active = true; headless::quitRequested = false;
    internal::appExitCode() = 0; internal::clearExitReason();
}
bool stoppedDelivery = false;
struct Stopping : App {
    void exit() override {
        std::thread worker([] { exitApp(27); }); worker.join();
        internal::drainMainThreadQueue();
        stoppedDelivery = !headless::quitRequested && internal::appExitCode() == 0;
    }
};
int main() {
    std::fputs("ENTERED MAIN\n", stderr);
    getMainThreadId();
    int defects=0;
    // No run has ever started: a worker must not leave any queue entry.
    std::thread noRun([] { exitApp(31); }); noRun.join();
    defects += !internal::mainThreadQueue().empty();
    for (int after=0; after<2; ++after) {
        internal::beginRuntimeRun(); internal::beginFrameworkLifetime(); reset();
        if(after) internal::shutdownRuntime();
        std::thread worker([]{exitApp(13);});worker.join();
        if (after) defects += !internal::mainThreadQueue().empty();
        if(!after) internal::shutdownRuntime();
        const auto epoch=internal::runtimeEpoch();
        internal::beginRuntimeRun(); reset();
        internal::drainMainThreadQueue();
        bool stale=headless::quitRequested || internal::appExitCode()!=0;
        defects+=stale;
        std::printf("QUEUE_EPOCH posted_%s_shutdown=%d epochs=%llu->%llu new_run_quit=%d new_run_code=%d\n",after?"after":"before",stale,(unsigned long long)epoch,(unsigned long long)internal::runtimeEpoch(),int(headless::quitRequested.load()),internal::appExitCode());
        internal::shutdownRuntime();
    }
    // A queued request delivered during App::exit must be inert even though
    // this run's token is not invalidated until shutdown finishes.
    internal::beginRuntimeRun(); reset();
    auto w = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*w, 815);
    internal::retainRuntimeWindow(w);
    w->setApp<Stopping>(); internal::applyPendingAppForTests(*w);
    internal::shutdownRuntime();
    defects += !stoppedDelivery;
    std::printf("QUEUE_EPOCH delivery_during_stopping_inert=%d\n", stoppedDelivery);
    // Exercise publication while workers post across repeated run boundaries.
    std::atomic<bool> done{false};
    std::thread racing([&] { while (!done.load()) { exitApp(29); std::this_thread::yield(); } });
    for (int i=0; i<200; ++i) { internal::beginRuntimeRun(); internal::shutdownRuntime(); }
    done = true; racing.join();
    internal::beginRuntimeRun(); reset(); internal::drainMainThreadQueue();
    defects += headless::quitRequested || internal::appExitCode()!=0;
    internal::shutdownRuntime();
    reset(); std::thread worker([]{exitApp(27);});worker.join();
    defects += !internal::mainThreadQueue().empty();
    internal::drainMainThreadQueue();
    defects += headless::quitRequested || internal::appExitCode()!=0;
    std::printf("QUEUE_EPOCH delivered_while_stopped quit=%d code=%d defects=%d\n",int(headless::quitRequested.load()),internal::appExitCode(),defects);
    headless::active=false;
    internal::endFrameworkLifetime();
    return defects ? 17 : 0;
}
