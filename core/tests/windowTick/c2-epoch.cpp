#include <TrussC.h>
#include <cstdio>
#include <thread>
using namespace tc;
int secondRequests = 0;
struct First : App {
    void setup() override { requestExitApp(); }
    void exit() override {
        std::thread worker([] { exitApp(7); });
        worker.join();
    }
};
struct Second : App {
    EventListener request;
    void setup() override {
        request = events().exitRequested.listen([](ExitRequestEventArgs&) { ++secondRequests; });
    }
    void update() override {
        // A timeout for an unaffected second run; this code must win.
        if (getUpdateCount() >= 3) exitApp(0);
    }
};
int main() {
    std::fputs("ENTERED MAIN\n", stderr);
    int first = runHeadlessApp<First>(HeadlessSettings().setFps(1000));
    auto queued = internal::mainThreadQueue().size();
    int second = runHeadlessApp<Second>(HeadlessSettings().setFps(1000));
    std::printf("EPOCH_PROBE first=%d pending_after_shutdown=%zu second=%d second_requests=%d\n", first, queued, second, secondRequests);
    return first == 0 && second == 0 && secondRequests == 1 ? 0 : 17;
}
