// Headless check of VideoGrabber's device lifecycle rules (#415): isConnected(),
// setup() with no arguments, close(), onDisconnect. No camera is needed: the
// test plays the backend through VideoGrabberTestAccess::reportLost(), which is
// what a capture thread or an OS notification calls when the camera is lost.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcVideoGrabberTestAccess.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {
using namespace tc;
using Access = internal::VideoGrabberTestAccess;

int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Counts onDisconnect and remembers what the first call saw
struct Recorder {
    int count = 0;
    std::string reason;
    bool connectedInside = true;   // isConnected() inside the first call
    bool openInside = true;        // the grabber still holds its buffers inside the first call
    bool onMainThread = true;
    EventListener sub;

    void attach(VideoGrabber& grabber, Deliver deliver = Deliver::Inline) {
        sub = grabber.onDisconnect.listen([this, &grabber](DeviceDisconnectEventArgs& e) {
            if (++count == 1) {
                reason = e.reason;
                connectedInside = grabber.isConnected();
                openInside = grabber.getWidth() != 0;
                onMainThread = isMainThread();
            }
        }, deliver);
    }
};

int errorLogs = 0;
} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Error) ++errorLogs;
    });

    // --- Rule 1: isConnected() is true only while the device is open -------
    {
        VideoGrabber g;
        check("1. a new grabber is not connected", !g.isConnected() && !g.isInitialized());
        Access::prepare(g);
        check("1. an open grabber is connected (isInitialized() is an alias)",
              g.isConnected() && g.isInitialized());
        Access::reportLost(g, "unplugged");
        check("1. a reported loss turns isConnected() false before update() runs",
              !g.isConnected() && !g.isInitialized());
        g.close();
        check("1. close() leaves it not connected", !g.isConnected());
    }

    // --- Rule 4: onDisconnect fires once, from update(), already closed ----
    {
        VideoGrabber g;
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        g.update();
        check("4. no loss, no event", rec.count == 0 && g.isConnected());
        Access::reportLost(g, "unplugged");
        check("4. the backend only records the loss; nothing fires yet", rec.count == 0);
        g.update();
        check("4. update() reports it once, with the reason", rec.count == 1 && rec.reason == "unplugged");
        check("4. ... on the main thread, with isConnected() already false",
              rec.onMainThread && !rec.connectedInside);
        check("4. ... and the grabber already closed (a listener may call setup())", !rec.openInside);
        check("4. ... and the grabber has closed itself", !g.isConnected() && g.getWidth() == 0);
        g.update();
        g.update();
        check("4. ... never again", rec.count == 1);
    }
    {
        // The first reported loss wins; a second report changes nothing
        VideoGrabber g;
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        Access::reportLost(g, "first");
        Access::reportLost(g, "second");
        g.update();
        check("4. a second report of the same run keeps the first reason",
              rec.count == 1 && rec.reason == "first");
    }
    {
        // A loss found on the capture thread reaches a Deliver::Main listener
        VideoGrabber g;
        Recorder rec;
        rec.attach(g, Deliver::Main);
        Access::prepare(g);
        std::thread capture([&] { Access::reportLost(g, "capture thread"); });
        capture.join();
        check("4. a loss found off the main thread fires nothing by itself", rec.count == 0);
        g.update();
        internal::drainMainThreadQueue();
        check("4. ... update() fires it once, on the main thread",
              rec.count == 1 && rec.onMainThread && rec.reason == "capture thread");
    }
    {
        // A listener may reconnect: the grabber is closed before it runs
        VideoGrabber g;
        g.setDeviceID(99);  // no such camera on any platform
        int calls = 0;
        bool connectedBefore = true;
        EventListener sub = g.onDisconnect.listen([&](DeviceDisconnectEventArgs&) {
            ++calls;
            connectedBefore = g.isConnected();
            g.setup();  // fails here (no camera); must not fire again
        });
        Access::prepare(g);
        Access::reportLost(g, "unplugged");
        g.update();
        g.update();
        check("4. a listener that calls setup() runs once and sees a closed grabber",
              calls == 1 && !connectedBefore);
        g.close();
    }

    // --- Rule 3: close() never fires onDisconnect --------------------------
    {
        VideoGrabber g;
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        g.close();
        g.update();
        check("3. close() of an open grabber fires nothing", rec.count == 0 && !g.isConnected());
        g.close();
        check("3. close() of a closed grabber fires nothing", rec.count == 0);
    }
    {
        // close() between the loss and update(): the app closed it first
        VideoGrabber g;
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        Access::reportLost(g, "unplugged");
        g.close();
        g.update();
        check("3. close() after an unreported loss drops it", rec.count == 0 && !g.isConnected());
    }
    {
        // A loss a late backend callback records after close() is never seen
        VideoGrabber g;
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        auto oldRun = Access::captureState(g);
        g.close();
        oldRun->reportLost("late callback");
        check("3. a report for a closed run is ignored", !oldRun->lost.load());
        Access::prepare(g);
        g.update();
        check("3. ... and does not reach the next run", rec.count == 0 && g.isConnected());
        g.close();
    }
    {
        // setup() closes an open grabber first, silently
        VideoGrabber g;
        g.setDeviceID(99);
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        g.setup(320, 240);
        check("3. setup() on an open grabber closes it without onDisconnect", rec.count == 0);
        g.close();
    }

    // --- Rule 5: nothing fires from the destructor -------------------------
    {
        Recorder rec;
        {
            VideoGrabber g;
            rec.attach(g);
            Access::prepare(g);
        }
        check("5. the destructor of an open grabber fires nothing", rec.count == 0);
    }
    {
        Recorder rec;
        {
            VideoGrabber g;
            rec.attach(g);
            Access::prepare(g);
            Access::reportLost(g, "unplugged");
        }
        check("5. ... nor of one whose loss was not reported yet", rec.count == 0);
    }
    {
        // The listener belongs to the grabber it was added to; a move carries
        // the open run and a pending loss, not the listeners
        VideoGrabber g;
        Recorder rec;
        rec.attach(g);
        Access::prepare(g);
        Access::reportLost(g, "unplugged");
        VideoGrabber moved(std::move(g));
        check("5. a move does not fire", rec.count == 0);
        check("1. the loss travels with the run: the moved-from grabber is clean, the new one lost",
              !g.isConnected() && !moved.isConnected() && !Access::sharedState(g)->lost.load() &&
              Access::sharedState(moved)->lost.load());
        moved.update();
        check("4. update() on the new owner closes it; the old owner's listener stays with the old owner",
              rec.count == 0 && !moved.isConnected());
        Recorder rec2;
        VideoGrabber other;
        rec2.attach(other);
        Access::prepare(other);
        Access::reportLost(other, "unplugged");
        VideoGrabber target;
        target = std::move(other);
        target.update();
        check("5. a move assignment does not fire either", rec2.count == 0);
    }

    // --- Rule 2: setup() with no arguments reopens the last device ---------
#if !defined(__EMSCRIPTEN__)
    {
        VideoGrabber g;
        check("2. before any setup, the last settings are 640x480",
              Access::lastWidth(g) == 640 && Access::lastHeight(g) == 480);
        g.setDeviceID(99);  // no such camera: every setup() fails
        const int before = errorLogs;
        bool ok = g.setup(320, 240);
        check("2. setup(w, h) on a missing camera returns false", !ok && !g.isConnected());
        check("2. ... and remembers the size", Access::lastWidth(g) == 320 && Access::lastHeight(g) == 240);
        g.close();  // also cancels a pending permission request (macOS)
        const int afterFirst = errorLogs;
        ok = g.setup();
        check("2. setup() tries the same device again and returns false", !ok && !g.isConnected());
        check("2. ... with the last size", Access::lastWidth(g) == 320 && Access::lastHeight(g) == 240 &&
              g.getDeviceID() == 99);
#if defined(__linux__) || defined(_WIN32)
        check("2. ... and logs an error each time", afterFirst > before && errorLogs > afterFirst);
#else
        (void)before; (void)afterFirst;
#endif
        g.close();

        VideoGrabber moved(std::move(g));
        check("2. a move carries the last size", Access::lastWidth(moved) == 320 && Access::lastHeight(moved) == 240);
    }
#endif

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
