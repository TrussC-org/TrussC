// #258: headless policy tests by default; optional --window-* smoke tests
// exercise runApp/cleanup under a real display or Xvfb. No forced GPU reset.
#include <TrussC.h>
#include "sokol/util/sokol_d3d11_device_loss.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <string>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}
constexpr uint32_t removed = 0x887A0005;
constexpr uint32_t reset = 0x887A0007;
constexpr uint32_t hung = 0x887A0006;
void notifyLoss() {
    sapp_event ev{};
    ev.type = SAPP_EVENTTYPE_TC_DEVICE_LOST;
    ev.device_lost_reason = hung; // reason differs from the triggering HRESULT
    internal::_event_cb(&ev);
}

string mode;
int updates = 0;
bool cleanedUp = false;
class SmokeApp : public App {
    EventListener loss;
public:
    void setup() override {
        if (mode == "--window-cancel") {
            loss = events().deviceLost.listen([](DeviceLostEventArgs& args) {
                check(args.reason == hung, "window reason preserved");
                args.cancel = true;
            });
        }
    }
    void update() override {
        ++updates;
        if (mode == "--window-code") exitApp(23);
        else if (mode == "--window-zero") exitApp();
        else if (updates == 1) notifyLoss();
        else exitApp(23); // cancellation must allow another update
    }
    void exit() override { cleanedUp = true; }
};

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1) {
        mode = argv[1];
        if (mode != "--window-default" && mode != "--window-cancel" &&
            mode != "--window-code" && mode != "--window-zero") return 2;
        const int code = runApp<SmokeApp>(WindowSettings().setSize(64, 64));
        const int expected = mode == "--window-default" ? 1 : mode == "--window-zero" ? 0 : 23;
        check(code == expected, "runApp returns exitApp code");
        check(cleanedUp, "normal App cleanup runs");
        check(updates == (mode == "--window-cancel" ? 2 : 1), "cancel controls continued updates");
        std::printf("deviceLost %s: runApp=%d, updates=%d, cleanup=%d\n",
                    mode.c_str(), code, updates, cleanedUp);
        return failures ? 1 : 0;
    }

    for (uint32_t hr : {0u, 0x087A0001u, 0x087A0007u, 0x887A000Au, hung, 0x80004005u}) {
        bool notified = false;
        check(!_sapp_tc_d3d11_is_device_loss(hr), "success/status/busy/unrelated failure is not loss");
        check(!_sapp_tc_d3d11_first_device_loss(hr, &notified) && !notified,
              "unrelated results do not consume notification");
    }
    for (uint32_t first : {removed, reset}) {
        bool notified = false;
        check(_sapp_tc_d3d11_is_device_loss(first), "removed/reset classified as loss");
        check(_sapp_tc_d3d11_first_device_loss(first, &notified), "first loss notifies");
        check(!_sapp_tc_d3d11_first_device_loss(removed, &notified), "repeat/reentrant removed suppressed");
        check(!_sapp_tc_d3d11_first_device_loss(reset, &notified), "second swapchain reset suppressed");
    }
    // Exercise the backend's shared pending-event gate without a GPU/display.
    // A resize can latch loss before setup has installed the app listener.
    for (uint32_t first : {removed, reset}) {
        bool notified = false;
        bool pending = false;
        bool initialized = false;
        int deferredCalls = 0;
        auto dispatchPending = [&] {
            if (_sapp_tc_d3d11_take_pending_device_loss(initialized, &pending)) notifyLoss();
        };
        check(!_sapp_tc_d3d11_take_pending_device_loss(true, &pending),
              "normal init has no pending loss");
        if (_sapp_tc_d3d11_first_device_loss(first, &notified)) pending = true;
        dispatchPending();
        check(notified && pending, "pre-init loss remains latched and pending");
        auto deferred = events().deviceLost.listen([&](DeviceLostEventArgs& args) {
            ++deferredCalls;
            check(initialized && args.reason == hung, "deferred loss delivered after init with reason");
            check(!pending, "pending cleared before app callback");
            check(!_sapp_tc_d3d11_first_device_loss(reset, &notified),
                  "reentrant loss cannot queue another event");
            dispatchPending();
            args.cancel = true;
        });
        dispatchPending();
        check(deferredCalls == 0 && pending, "no notification before init completes");
        initialized = true;
        dispatchPending();
        dispatchPending(); // another tick must not replay the notification
        check(deferredCalls == 1 && !pending, "pre-init loss delivered exactly once after init");
    }
    int lossCalls = 0;
    int errorLogs = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.level == LogLevel::Error && args.message.find("887a0006") != string::npos)
            ++errorLogs;
    });
    auto cancel = events().deviceLost.listen([&](DeviceLostEventArgs& args) {
        ++lossCalls;
        check(args.reason == hung && !args.cancel, "reason and cancel defaults");
        args.cancel = true;
    });
    internal::appExitCode() = 0;
    notifyLoss();
    check(lossCalls == 1 && internal::appExitCode() == 0 && errorLogs == 0,
          "cancellation leaves exit code unchanged");
    cancel.disconnect();
    // An exitRequested listener cannot cancel the default device-loss exit.
    auto exitCancel = events().exitRequested.listen([](ExitRequestEventArgs& args) { args.cancel = true; });
    notifyLoss();
    check(internal::appExitCode() == 1 && errorLogs == 1, "default logs reason and selects failure exit");
    exitApp(23);
    check(internal::appExitCode() == 23, "explicit exit code");
    exitApp();
    check(internal::appExitCode() == 0, "no-argument exit remains success");
    std::printf("deviceLost headless: %d failures\n", failures);
    return failures ? 1 : 0;
}
