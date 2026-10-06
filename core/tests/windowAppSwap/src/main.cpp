// =============================================================================
// core/tests/windowAppSwap — behavioral regression test for #315: a
// secondary window's setApp() and close() are requests that take effect at
// the window's next frame boundary, wherever they are called from.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// A Window made in a headless test has no native side. OpenWindow stands in
// for an open one, and tick() / pressKey() drive it the way the platform glue
// does (window context active, internal::WindowDispatchScope around the tick
// or the event). Build with -fsanitize=address to check the memory side.
//
// Guards the invariants:
//   - setApp() from the window's own App, in its update() or keyPressed() or
//     from a child node's update() / onKeyPress(): the call returns with the
//     window unchanged (getApp() and App::getWindow() as before), the App and
//     its children stay alive for the rest of the tick or event, and the swap
//     lands when the tick / event ends. The outgoing App, held only by the
//     window, is gone after that; the incoming one gets setup() on its first
//     tick.
//   - A request made outside a tick lands when the next tick starts.
//   - Two setApp() before one boundary: the last one wins; the other App is
//     never attached (no setup(), not in the double-attach guard).
//   - close() with a setApp() before the boundary: close() wins, the pending
//     App is dropped with a warning, and isOpen() / getApp() stay as they were
//     until the close lands. setApp() after close() logs an error at the call.
//   - The checks also run when the request is applied: the same new App
//     requested on two windows ends up on the first one only.
//   - The teardown detaches the App before its exit() / cleanup() run:
//     setApp() and close() from the App's own exit() / cleanup() find a
//     closed window with no App.
// --pipeline-cycles self|main|early [N] additionally exercises real native
// close callbacks and checks live GPU resources under X11 / Win32 / macOS.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "pipelineCycles.h"

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-78s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// Stand-in for the platform's native state. Its first member reads as the
// sapp_window handle the platform close() passes to sokol: id 0 is the
// invalid handle, so the request reaches no native window.
static uint64_t g_nativeStandIn = 0;

struct OpenWindow : Window {
    OpenWindow() { native_ = &g_nativeStandIn; }
    // What the platform teardown does with the App, without the native part.
    void landClose() {
        native_ = nullptr;
        closeRequested_ = false;
        internal::WindowRequestAccess::endApp(*this);
    }
    ~OpenWindow() { if (native_) landClose(); }
};

// One tick of a window, as the platform glue runs it.
static void tick(Window& w) {
    internal::WindowContext* prev = internal::currentWindowCtx();
    internal::currentWindowCtx() = &w.context();
    {
        internal::WindowDispatchScope scope(w);
        w.events().update.notify();
        w.tickTree();
    }
    internal::currentWindowCtx() = prev;
}

// One key-down event of a window, as the platform glue dispatches it.
static void pressKey(Window& w, int key) {
    internal::WindowContext* prev = internal::currentWindowCtx();
    internal::currentWindowCtx() = &w.context();
    {
        internal::WindowDispatchScope scope(w);
        KeyEventArgs e;
        e.key = key;
        w.events().keyPressed.notify(e);
        if (w.getApp()) w.getApp()->keyPressed(e);
        if (!e.consumed) w.dispatchKeyPressToTree(e);
    }
    internal::currentWindowCtx() = prev;
}

// --- Apps --------------------------------------------------------------------

struct Counts {
    int setups = 0, updates = 0, exits = 0, cleanups = 0, destroyed = 0;
};

class CountingApp : public App {
public:
    explicit CountingApp(Counts& c) : c_(c) {}
    ~CountingApp() override { ++c_.destroyed; }
    void setup() override { ++c_.setups; }
    void update() override { ++c_.updates; }
    void exit() override { ++c_.exits; }
    void cleanup() override { ++c_.cleanups; }
protected:
    Counts& c_;
};

// What a request made from inside the App saw. Kept outside the App, which
// is gone by the time the checks read it.
struct Record {
    bool appGone = false;          // set by ~SwappingApp
    bool getAppSame = false;       // window.getApp() right after the request
    bool getWindowSame = false;    // App::getWindow() right after the request
    bool isOpenAfter = false;      // window.isOpen() right after the request
    int childRunsAfter = 0;        // child update / key handled after the request
    bool childSawGone = false;     // the child ran after its App was destroyed
    bool requested = false;
};

// The App's child: runs after the App's own update() / keyPressed() in the
// same tick or event, and records whether its App was still alive.
class Watcher : public Node {
public:
    Record* rec = nullptr;
    function<void()> onUpdate, onKey;
    void update() override {
        if (onUpdate) { auto f = onUpdate; onUpdate = nullptr; f(); }
        note();
    }
    bool onKeyPress(int key) override {
        (void)key;
        if (onKey) { auto f = onKey; onKey = nullptr; f(); }
        note();
        return false;
    }
private:
    void note() {
        if (!rec || !rec->requested) return;
        ++rec->childRunsAfter;
        if (rec->appGone) rec->childSawGone = true;
    }
};

// An App whose update() / keyPressed() run a one-shot request, then use the
// App and its window again: the use that must stay valid.
class SwappingApp : public CountingApp {
public:
    SwappingApp(Counts& c, Record& rec, Window& win) : CountingApp(c), rec_(rec), win_(win) {}
    ~SwappingApp() override { rec_.appGone = true; }
    function<void()> onUpdate, onKey;
    shared_ptr<Watcher> watcher;
    void setup() override {
        CountingApp::setup();
        watcher = make_shared<Watcher>();
        watcher->rec = &rec_;
        addChild(watcher);
    }
    void update() override {
        CountingApp::update();
        if (onUpdate) { auto f = onUpdate; onUpdate = nullptr; f(); afterRequest(); }
    }
    void keyPressed(int key) override {
        (void)key;
        if (onKey) { auto f = onKey; onKey = nullptr; f(); afterRequest(); }
    }
private:
    void afterRequest() {
        rec_.requested = true;
        rec_.getAppSame = win_.getApp().get() == this;
        rec_.getWindowSame = getWindow() == &win_;
        rec_.isOpenAfter = win_.isOpen();
    }
    Record& rec_;
    Window& win_;
};

// Makes a SwappingApp the window's App (the window is its only owner) and
// runs its first tick, so setup() has added the child.
static SwappingApp* attachSwapping(Window& win, Counts& c, Record& rec) {
    win.setApp(make_shared<SwappingApp>(c, rec, win));
    tick(win);
    return static_cast<SwappingApp*>(win.getApp().get());
}

class ExitRequestApp : public CountingApp {
public:
    ExitRequestApp(Counts& c, Window& win) : CountingApp(c), win_(win) {}
    shared_ptr<App> other;
    bool getAppEmptyInExit = false, getWindowEmptyInExit = false, closedInExit = false;
    void exit() override {
        CountingApp::exit();
        getAppEmptyInExit = win_.getApp() == nullptr;
        getWindowEmptyInExit = getWindow() == nullptr;
        closedInExit = !win_.isOpen();
        win_.setApp(other);
        win_.close();
        win_.setApp(nullptr);
    }
    void cleanup() override {
        CountingApp::cleanup();
        win_.setApp(other);
        win_.close();
    }
private:
    Window& win_;
};

} // namespace

// -----------------------------------------------------------------------------

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc >= 3 && string(argv[1]) == "--pipeline-cycles") {
        return pipelineCycles::run(argv[2], argc > 3 ? std::atoi(argv[3]) : 100);
    }
    getMainThreadId();   // this thread is the main thread

    vector<string> warnings, errors;
    EventListener logSub = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) warnings.push_back(e.message);
        if (e.level == LogLevel::Error) errors.push_back(e.message);
    });
    auto count = [](const vector<string>& list, const string& needle) {
        size_t n = 0;
        for (auto& m : list) if (m.find(needle) != string::npos) ++n;
        return n;
    };
    auto& attached = internal::attachedApps();

    // --- a request from outside a tick ------------------------------------------
    {
        Counts c;
        OpenWindow win;
        auto app = make_shared<CountingApp>(c);
        win.setApp(app);
        check("setApp() outside a tick: getApp() unchanged until the boundary",
              win.getApp() == nullptr && !attached.count(app.get()));
        tick(win);
        check("... it lands when the next tick starts (setup() and update() ran)",
              win.getApp() == app && attached.count(app.get()) && c.setups == 1 && c.updates == 1);
    }

    // Probes above each Window outlive its teardown, including App destruction.
    // --- nullptr is a pending release, not the absence of a request ------------
    {
        Counts c;
        OpenWindow win;
        auto app = make_shared<CountingApp>(c);
        win.setApp(app);
        tick(win);
        win.setApp(nullptr);
        check("setApp(nullptr): current App stays until the boundary", win.getApp() == app);
        tick(win);
        check("... boundary releases the App and its attachment guard",
              win.getApp() == nullptr && !attached.count(app.get()));
    }

    // --- requests cannot land inside a nested event / tick --------------------
    {
        Counts oldC, nextC;
        OpenWindow win;
        auto old = make_shared<CountingApp>(oldC);
        auto next = make_shared<CountingApp>(nextC);
        win.setApp(old);
        tick(win);
        {
            internal::WindowDispatchScope outer(win);
            win.setApp(next);
            pressKey(win, 'Q');
            check("nested event: pending swap stays pending inside the outer tick",
                  win.getApp() == old);
        }
        check("... the outer boundary applies the swap", win.getApp() == next);
    }

    // --- apply-time validation also rejects an App ended since the request ----
    {
        Counts c;
        OpenWindow win;
        auto app = make_shared<CountingApp>(c);
        const size_t errs = count(errors, "already ran cleanup()");
        win.setApp(app);
        app->cleanup();
        internal::detachAppAudio(*app);   // marks the lifecycle as ended
        tick(win);
        check("App ended after request: apply refuses it and logs the reason",
              !win.getApp() && c.setups == 0 &&
              count(errors, "already ran cleanup()") == errs + 1);
    }

    // --- a request dies with its window, without running the pending App ------
    {
        Counts c;
        weak_ptr<App> pending;
        {
            OpenWindow win;
            auto app = make_shared<CountingApp>(c);
            pending = app;
            win.setApp(app);
        }
        check("destroyed Window releases its pending App without setup / exit / cleanup",
              pending.expired() && c.destroyed == 1 && c.setups == 0 &&
              c.exits == 0 && c.cleanups == 0 && attached.empty());
    }

    // --- setApp() from the App's own update(), the window its only owner -------
    {
        Counts oldC, nextC;
        Record rec;
        OpenWindow win;
        SwappingApp* old = attachSwapping(win, oldC, rec);
        weak_ptr<App> nextWeak;
        old->onUpdate = [&] {
            auto next = make_shared<CountingApp>(nextC);
            nextWeak = next;
            win.setApp(next);
        };
        tick(win);
        check("setApp() in update(): getApp() / getWindow() unchanged after the call",
              rec.getAppSame && rec.getWindowSame);
        check("... the App's child ran after it with the App alive",
              rec.childRunsAfter == 1 && !rec.childSawGone);
        check("... the swap landed when the tick ended; the old App is gone",
              rec.appGone && oldC.destroyed == 1 && win.getApp() && win.getApp() == nextWeak.lock());
        check("... the new App is attached, the old one is not",
              attached.count(win.getApp().get()) == 1 && attached.size() == 1);
        check("... the new App's setup() waits for its first tick", nextC.setups == 0);
        tick(win);
        check("... next tick: the new App's setup() and update()",
              nextC.setups == 1 && nextC.updates == 1);
    }

    // --- setApp() from the App's own keyPressed() --------------------------------
    {
        Counts oldC, nextC;
        Record rec;
        OpenWindow win;
        SwappingApp* old = attachSwapping(win, oldC, rec);
        old->onKey = [&] { win.setApp(make_shared<CountingApp>(nextC)); };
        pressKey(win, 'Q');
        check("setApp() in keyPressed(): getApp() / getWindow() unchanged after the call",
              rec.getAppSame && rec.getWindowSame);
        check("... the key went on to the old App's child, with the App alive",
              rec.childRunsAfter == 1 && !rec.childSawGone);
        check("... the swap landed when the event ended",
              rec.appGone && win.getApp() && nextC.destroyed == 0);
    }

    // --- setApp() from a child's update() and onKeyPress() -----------------------
    {
        Counts oldC, nextC;
        Record rec;
        OpenWindow win;
        SwappingApp* old = attachSwapping(win, oldC, rec);
        bool goneInside = true;
        old->watcher->onUpdate = [&] {
            win.setApp(make_shared<CountingApp>(nextC));
            rec.requested = true;
            goneInside = rec.appGone || win.getApp().get() != old;
        };
        tick(win);
        check("setApp() in a child's update(): App alive and attached for the rest of it",
              !goneInside && rec.childRunsAfter == 1 && !rec.childSawGone);
        check("... the swap landed when the tick ended", rec.appGone && win.getApp());
    }
    {
        Counts oldC, nextC;
        Record rec;
        OpenWindow win;
        SwappingApp* old = attachSwapping(win, oldC, rec);
        bool goneInside = true;
        old->watcher->onKey = [&] {
            win.setApp(make_shared<CountingApp>(nextC));
            rec.requested = true;
            goneInside = rec.appGone || win.getApp().get() != old;
        };
        pressKey(win, 'Q');
        check("setApp() in a child's onKeyPress(): App alive and attached for the rest of it",
              !goneInside && !rec.childSawGone);
        check("... the swap landed when the event ended", rec.appGone && win.getApp());
    }

    // --- two setApp() before one boundary: the last wins -------------------------
    {
        Counts oldC, firstC, lastC;
        Record rec;
        OpenWindow win;
        SwappingApp* old = attachSwapping(win, oldC, rec);
        weak_ptr<App> firstWeak, lastWeak;
        old->onUpdate = [&] {
            auto first = make_shared<CountingApp>(firstC);
            auto last = make_shared<CountingApp>(lastC);
            firstWeak = first;
            lastWeak = last;
            win.setApp(first);
            win.setApp(last);
        };
        tick(win);
        tick(win);
        check("two setApp() in one frame: the last one is attached",
              win.getApp() && win.getApp() == lastWeak.lock() && lastC.setups == 1);
        check("... the first one was never attached and is gone",
              firstWeak.expired() && firstC.setups == 0 && firstC.destroyed == 1);
        check("... only the last one is in the double-attach guard",
              attached.size() == 1 && attached.count(win.getApp().get()) == 1);
    }

    // --- the window's own App requested again cancels a pending swap -------------
    {
        Counts c, otherC;
        OpenWindow win;
        auto app = make_shared<CountingApp>(c);
        win.setApp(app);
        tick(win);
        const size_t errs = errors.size();
        win.setApp(make_shared<CountingApp>(otherC));
        win.setApp(app);
        tick(win);
        check("setApp(current App) after a pending swap: the current App stays",
              win.getApp() == app && otherC.setups == 0 && errors.size() == errs);
    }

    // --- close() from the App's own update(), with a setApp() in the same frame --
    {
        Counts oldC, nextC;
        Record rec;
        OpenWindow win;
        SwappingApp* old = attachSwapping(win, oldC, rec);
        const size_t dropWarnings = count(warnings, "close() was requested");
        const size_t closingErrors = count(errors, "this window is closing");
        weak_ptr<App> nextWeak;
        old->onUpdate = [&] {
            auto next = make_shared<CountingApp>(nextC);
            nextWeak = next;
            win.setApp(next);   // before the close: pending, then dropped
            win.close();
            win.setApp(make_shared<CountingApp>(nextC));   // after it: refused
        };
        tick(win);
        check("close() in update(): isOpen() / getApp() / getWindow() unchanged after it",
              rec.isOpenAfter && rec.getAppSame && rec.getWindowSame);
        check("... the App and its child stay alive for the rest of the tick",
              rec.childRunsAfter == 1 && !rec.childSawGone && !rec.appGone);
        check("... close() wins: the pending setApp() is dropped, with a warning",
              nextWeak.expired() && nextC.setups == 0 &&
              count(warnings, "close() was requested") == dropWarnings + 1);
        check("... setApp() after close() logs an error at the call",
              count(errors, "this window is closing") == closingErrors + 1);
        check("... until the close lands: still open, same App, close requested",
              win.isOpen() && win.getApp().get() == old && internal::WindowRequestAccess::closeRequested(win));
        tick(win);
        check("... ticks before the close lands keep the App (no exit() yet)",
              oldC.exits == 0 && win.getApp().get() == old);
        win.landClose();   // what close_cb -> teardown() does with the App
        check("... the close: exit() and cleanup() once, the App is gone",
              oldC.exits == 1 && oldC.cleanups == 1 && rec.appGone && win.getApp() == nullptr);
        check("... nothing left in the double-attach guard", attached.empty());
    }

    // --- close() from the App's own keyPressed() ---------------------------------
    {
        Counts c;
        Record rec;
        OpenWindow win;
        SwappingApp* app = attachSwapping(win, c, rec);
        app->onKey = [&] { win.close(); };
        pressKey(win, 'Q');
        check("close() in keyPressed(): still open after it, the key reached the child",
              rec.isOpenAfter && rec.getAppSame && rec.childRunsAfter == 1 && !rec.appGone);
        win.close();
        check("... a second close() is the same request", internal::WindowRequestAccess::closeRequested(win) && win.isOpen());
        win.landClose();
        check("... the close: exit() / cleanup() once",
              c.exits == 1 && c.cleanups == 1 && rec.appGone);
    }

    // --- the same new App requested on two windows -------------------------------
    {
        Counts c;
        OpenWindow a, b;
        auto app = make_shared<CountingApp>(c);
        const size_t errs = count(errors, "already drives another window");
        a.setApp(app);
        b.setApp(app);   // accepted: not attached anywhere yet
        tick(a);
        tick(b);
        check("same App requested on two windows: only the first to apply takes it",
              a.getApp() == app && b.getApp() == nullptr && c.setups == 1);
        check("... the second logs the double attach when it applies",
              count(errors, "already drives another window") == errs + 1);
        a.landClose();
        b.landClose();
    }

    // --- setApp() / close() from the App's own exit() and cleanup() -------------
    {
        Counts c, otherC;
        OpenWindow win;
        auto app = make_shared<ExitRequestApp>(c, win);
        app->other = make_shared<CountingApp>(otherC);
        win.setApp(app);
        tick(win);
        const size_t closedErrors = count(errors, "this window is closed");
        win.landClose();
        check("exit(): the window already has no App and is closed",
              app->getAppEmptyInExit && app->getWindowEmptyInExit && app->closedInExit);
        check("... setApp() from exit() / cleanup() is refused with an error each",
              count(errors, "this window is closed") == closedErrors + 2);
        check("... exit() / cleanup() ran once; the window stays empty",
              c.exits == 1 && c.cleanups == 1 && win.getApp() == nullptr &&
              otherC.setups == 0 && !internal::WindowRequestAccess::closeRequested(win));
        check("... nothing left in the double-attach guard", attached.empty());
    }

    printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
