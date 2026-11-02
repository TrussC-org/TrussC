// Replacement-race regression from the C2 review, plus deferred delivery checks.
#include <TrussC.h>
#include <thread>
#include <atomic>
#include <cstdio>
using namespace tc;
int exitCalls=0;
std::atomic<unsigned> posted{0};
struct Empty : App { void exit() override { ++exitCalls; std::this_thread::sleep_for(std::chrono::milliseconds(1)); } };
int main() {
    std::fputs("ENTERED MAIN\n", stderr);
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    headless::active = true;
    auto window = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*window, 811);
    internal::retainRuntimeWindow(window);
    window->setApp<Empty>(); internal::applyPendingAppForTests(*window);
    std::atomic<bool> start{false}, done{false}, workerStarted{false};
    std::thread worker([&] {
        while (!start.load()) {}
        workerStarted=true;
        while (!done.load()) {
            if (posted.load() < 20000) { requestExitApp(); ++posted; }
            else std::this_thread::yield();
        }
    });
    start = true;
    while (!workerStarted.load()) {}
    for (int i=0; i<200; ++i) {
        window->setApp<Empty>(); internal::applyPendingAppForTests(*window);
    }
    done = true;
    worker.join();
    // Requests posted while an unrelated App exits must survive the ending.
    if (headless::quitRequested) return 21; // workers only post
    internal::drainMainThreadQueue();
    if (!headless::quitRequested || !posted.load()) return 22;
    headless::quitRequested = false;
    // Posting from an already-retiring App must not inherit its cancellation.
    auto owner = internal::appOwner(*window->getApp());
    owner->retiring = true;
    std::thread ownedWorker([owner] {
        internal::AppOwnerScope scope(owner);
        exitApp(5);
    });
    ownedWorker.join();
    if (headless::quitRequested || internal::appExitCode() != 0) return 23;
    internal::drainMainThreadQueue();
    if (!headless::quitRequested || internal::appExitCode() != 5) return 24;
    owner->retiring = false;
    headless::quitRequested = false;
    // A worker posting during synchronous notification is delivered afterwards.
    auto veto = runtime().events().exitRequested.listen([](ExitRequestEventArgs& args) {
        std::thread worker([] { exitApp(9); });
        worker.join();
        args.cancel = true;
    });
    if (internal::dispatchExitRequested() || headless::quitRequested) return 25;
    internal::drainMainThreadQueue();
    if (!headless::quitRequested || internal::appExitCode() != 9) return 26;
    veto = {};
    internal::shutdownRuntime();
    headless::active = false;
    std::printf("worker quit versus App replacement completed exits=%d\n",exitCalls);
}
