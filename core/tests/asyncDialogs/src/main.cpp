#include <TrussC.h>
#include "tc/utils/internal/tcAsyncDialog.h"
#include "../../common/tcCoreTest.h"
#include "../../common/tcWindowTest.h"
#include <cstdio>
#include <thread>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <gtk/gtk.h>
#endif

using namespace tc;
namespace {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
int failures = 0;
void check(const char* label, bool okay) {
    std::printf("asyncDialogs: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
void prelude() { internal::HostTurnScope turn; }
std::vector<internal::DialogCompletion> replies;
std::vector<internal::DialogInputs> presented;
int dismissed = 0;
bool failPresentation = false;
internal::DialogDismiss fakePresenter(const internal::DialogInputs& inputs, internal::DialogCompletion done) {
    check("presentation is on main", isMainThread());
    presented.push_back(inputs);
    if (failPresentation) { done({}); return {}; }
    replies.push_back(std::move(done));
    return [] { ++dismissed; };
}
struct Probe : App { int updates = 0; void update() override { ++updates; } };
void attach(Window& window) {
    window.setApp<Probe>();
    internal::applyPendingAppForTests(window);
}
void tick(Window& window) {
    auto* previous = internal::currentWindowCtx();
    internal::currentWindowCtx() = &internal::WindowRequestAccess::context(window);
    { internal::WindowEntryScope entry(window); if (entry) window.events().update.notify(); }
    internal::currentWindowCtx() = previous;
}
void fakeTests() {
    internal::beginRuntimeRun();
    internal::beginFrameworkLifetime();
    internal::setDialogPresenterForTests(fakePresenter);
    {
        tcCoreTest::WindowFixture first(101), second(202);
        internal::retainRuntimeWindow(first.window);
        internal::retainRuntimeWindow(second.window);
        attach(*first.window); attach(*second.window);
        auto app = std::static_pointer_cast<Probe>(first.window->getApp());
        auto peer = std::static_pointer_cast<Probe>(second.window->getApp());
        int calls = 0;
        bool contextOkay = false;
        {
            internal::AppOwnerScope scope(internal::appOwner(*app));
            std::string title = "copied title";
            confirmDialogAsync(title, "body", [&](bool yes) {
                ++calls;
                contextOkay = yes && isMainThread() &&
                    internal::currentAppOwner() == internal::appOwner(*app) &&
                    internal::currentWindowCtx() == &internal::WindowRequestAccess::context(*first.window);
            });
            title = "changed";
        }
        check("call returns before presentation and callback", presented.empty() && calls == 0);
        prelude();
        check("next prelude presents copied inputs", presented.size() == 1 && presented[0].title == "copied title" && calls == 0);
        const int firstUpdates = app->updates, peerUpdates = peer->updates;
        for (int i = 0; i < 3; ++i) { prelude(); tick(*first.window); tick(*second.window); }
        check("two windows keep ticking while dialog is open", app->updates == firstUpdates + 3 && peer->updates == peerUpdates + 3 && calls == 0);
        FileDialogResult accepted; accepted.success = true;
        std::thread producer([&] { replies[0](accepted); }); producer.join();
        check("native result only posts", calls == 0);
        prelude();
        check("later prelude delivers on main with App context", calls == 1 && contextOkay);

        // Each kind has its own outstanding request, including no callback.
        const size_t start = presented.size(), replyStart = replies.size();
        int alerts = 0, confirms = 0, files = 0;
        alertDialogAsync("alert", "body", [&] { ++alerts; });
        confirmDialogAsync("confirm", "body", [&](bool yes) { if (!yes) ++confirms; });
        loadDialogAsync("load", "body", "/tmp", true, [&](const FileDialogResult& result) { if (!result.success && result.filePath.empty()) ++files; });
        saveDialogAsync("save", "body", "/tmp", "name.txt", [&](const FileDialogResult& result) { if (result.success && result.fileName == "saved.txt") ++files; });
        check("all Async calls defer", presented.size() == start && alerts + confirms + files == 0);
        prelude();
        check("concurrent requests present independently", presented.size() == start + 4 && presented[start + 2].folderSelection && presented[start + 3].defaultName == "name.txt");
        replies[replyStart]({}); replies[replyStart + 1]({}); replies[replyStart + 2]({});
        FileDialogResult saved; saved.success = true; saved.filePath = "/tmp/saved.txt"; saved.fileName = "saved.txt";
        replies[replyStart + 3](saved);
        prelude();
        check("alert completion, confirm/load cancel and save success", alerts == 1 && confirms == 1 && files == 2);

        failPresentation = true;
        int failed = 0;
        alertDialogAsync("", "", [&] { ++failed; });
        confirmDialogAsync("", "", [&](bool yes) { if (!yes) ++failed; });
        loadDialogAsync("", "", {}, false, [&](const FileDialogResult& result) { if (!result.success) ++failed; });
        saveDialogAsync("", "", {}, {}, [&](const FileDialogResult& result) { if (!result.success) ++failed; });
        check("failure cannot complete inline", failed == 0);
        prelude(); check("immediate native failure waits another prelude", failed == 0);
        prelude(); check("all failure results are cancellation", failed == 4);
        failPresentation = false;

        {
            tcCoreTest::WindowFixture requester(303);
            internal::retainRuntimeWindow(requester.window);
            int windowCalls = 0;
            auto listener = requester.window->events().keyPressed.listen([&](const KeyEventArgs&) {
                check("Window listener has no App owner", !internal::currentAppOwner());
                alertDialogAsync("pending", "", [&] { ++windowCalls; });
                alertDialogAsync("queued", "", [&] { ++windowCalls; });
            });
            const size_t windowStart = presented.size(), windowReplies = replies.size();
            sapp_event key{}; key.type = SAPP_EVENTTYPE_KEY_DOWN; key.key_code = SAPP_KEYCODE_A;
            internal::dispatchWindowEventForTests(&key, *requester.window);
            // A different window is selected before presentation; the host
            // prelude itself runs in process context.
            internal::currentWindowCtx() = &internal::WindowRequestAccess::context(*second.window);
            prelude();
            auto presentedWindow = presented[windowStart].requestingWindow.lock();
            check("presenter retains requester identity after active window changes",
                  presented.size() == windowStart + 2 && presentedWindow == requester.window && presentedWindow->isOpen());
            replies[windowReplies + 1]({}); // One pending dialog, one queued result.
            const int beforeWindowClose = dismissed;
            internal::windowClosed(internal::WindowRequestAccess::binding(*requester.window).handle,
                                   requester.window.get()); // Production native-close endpoint.
            check("Window listener close dismisses pending and queued dialogs",
                  !requester.window->isOpen() && dismissed == beforeWindowClose + 2);
            replies[windowReplies]({}); // A late native response must also be dropped.
            prelude();
            check("Window listener close drops queued and late callbacks", windowCalls == 0);
        }

        int stale = 0;
        {
            internal::AppOwnerScope scope(internal::appOwner(*app));
            alertDialogAsync("", "", [&] { ++stale; });
        }
        prelude();
        replies.back()({}); // Result already queued when the requesting window closes.
        const int beforeClose = dismissed;
        internal::WindowRequestAccess::teardown(*first.window);
        prelude();
        check("owner window close dismisses and drops queued callback", stale == 0 && dismissed == beforeClose + 1);

        {
            internal::AppOwnerScope scope(internal::appOwner(*peer));
            alertDialogAsync("", "", [&] { ++stale; });
        }
        prelude();
        const int beforeSwap = dismissed;
        attach(*second.window); // The production App replacement boundary.
        replies.back()({}); prelude();
        check("App replacement dismisses and drops callback", stale == 0 && dismissed == beforeSwap + 1);
    }
    replies.clear(); presented.clear();
    int quitCalls = 0;
    alertDialogAsync("", "", [&] { ++quitCalls; }); // Runtime-owned request.
    prelude();
    const int beforeQuit = dismissed;
    internal::shutdownRuntime();
    replies.back()({});
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    prelude(); // Even a later run must not deliver the previous run's result.
    check("Linux quit dismisses and drops callback", quitCalls == 0 && dismissed == beforeQuit + 1);
    internal::shutdownRuntime();
    replies.clear();
    internal::setDialogPresenterForTests(nullptr);
}
int gtkSmoke(bool noDisplay) {
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    int calls = 0;
    alertDialogAsync("TrussC async smoke", "GTK response", [&] { ++calls; });
    check("real GTK call returns before completion", calls == 0);
    prelude();
    check("real presentation does not deliver inline", calls == 0);
    if (!noDisplay) {
        GList* windows = gtk_window_list_toplevels();
        int dialogs = 0;
        for (GList* item = windows; item; item = item->next) {
            if (GTK_IS_DIALOG(item->data)) {
                ++dialogs;
                // Dispatch through the prelude pump, after its mailbox snapshot.
                g_idle_add(+[](gpointer data) -> gboolean {
                    gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_OK);
                    return G_SOURCE_REMOVE;
                }, item->data);
            }
        }
        g_list_free(windows);
        check("real GTK dialog is present", dialogs == 1);
        prelude();
        check("GTK pump response waits for following prelude", calls == 0);
    }
    prelude();
    check(noDisplay ? "gtk_init_check failure completes later" : "real response handler completes later", calls == 1);
    internal::shutdownRuntime();
    return failures ? 1 : 0;
}
#endif
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    getMainThreadId();
    if (argc > 1) return gtkSmoke(std::string(argv[1]) == "--no-display");
    fakeTests();
    return failures ? 1 : 0;
#else
    (void)argc; (void)argv;
    std::puts("asyncDialogs: Linux slice; other platforms unchanged");
    return 0;
#endif
}
