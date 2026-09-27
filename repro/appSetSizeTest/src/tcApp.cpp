// =============================================================================
// appSetSizeTest - App::setSize() must resize the App's OWN window
// =============================================================================
// App::setSize() resizes a window. The question is WHICH one: it must be the
// window the App is attached to, no matter which window's callback makes the
// call. Each cross-window call is compared against a reference — the same
// call made from the App's own window context, which has always worked —
// so the check holds for pixel-perfect mode and for platforms where the main
// window cannot be resized (then both sides are a no-op). It also checks that
// the OTHER window did not change.
//
// Runs unattended, logs [PASS]/[FAIL] per check, prints RESULT and quits.
// TC_PIXEL_PERFECT=1 runs the same checks in pixel-perfect mode (see main.cpp).

#include "tcApp.h"

namespace {
const IVec2 kSecondA{360, 240};
const IVec2 kSecondB{480, 300};
string str(IVec2 v) { return toString(v.x) + "x" + toString(v.y); }
bool same(IVec2 a, IVec2 b) { return a.x == b.x && a.y == b.y; }
}

// --- SubApp ------------------------------------------------------------------

void SubApp::update() {
    auto todo = std::move(jobs);
    jobs.clear();
    for (auto& job : todo) job();   // runs in THIS window's context
}

void SubApp::draw() {
    clear(0.1f, 0.12f, 0.2f);
    setColor(1.0f);
    drawBitmapString("second window " + toString((int)getWidth()) + "x" + toString((int)getHeight()), 20, 30);
}

// --- tcApp -------------------------------------------------------------------

void tcApp::check(const string& id, bool ok, const string& what) {
    const string line = string(ok ? "[PASS] " : "[FAIL] ") + id + "  " + what;
    (ok ? passed : failed)++;
    lines.push_back(line);
    if (ok) logNotice("appSetSizeTest") << line;
    else    logError("appSetSizeTest") << line;
}

IVec2 tcApp::mainSize() const { return { getWindowWidth(), getWindowHeight() }; }
IVec2 tcApp::secondSize() const { return { second->getWidth(), second->getHeight() }; }

// A resize is asynchronous (the request goes through the OS / window manager),
// so wait until both windows have held the same size for 15 frames. At least
// 30 frames always pass, so a resize of the WRONG window has time to show up.
bool tcApp::settled() {
    const IVec2 m = mainSize(), s = secondSize();
    if (same(m, lastMain) && same(s, lastSecond)) stableFrames++;
    else stableFrames = 0;
    lastMain = m;
    lastSecond = s;
    return (stepFrames >= 30 && stableFrames >= 15) || stepFrames >= 300;
}

void tcApp::setup() {
    logNotice("appSetSizeTest") << "pixel-perfect mode: " << (internal::pixelPerfectMode ? "ON" : "off")
                                << "  dpi scale: " << getDpiScale();
}

void tcApp::update() {
    stepFrames++;
    const int prevStep = step;

    switch (step) {
    case 0: {
        WindowSettings ws;
        ws.setSize(kSecondA.x, kSecondA.y);
        ws.setTitle("appSetSizeTest - second");
        second = createWindow(ws);
        if (!second) { check("setup", false, "createWindow failed"); step = 99; break; }
        sub = make_shared<SubApp>();
        second->setApp(sub);
        step = 1;
        break;
    }
    case 1:
        if (!settled()) break;
        mainStart = mainSize();
        secondStart = secondSize();
        logNotice("appSetSizeTest") << "start: main " << str(mainStart) << ", second " << str(secondStart);
        // Reference: the sub App resizes itself from its own context.
        sub->jobs.push_back([this] { sub->setSize((float)kSecondB.x, (float)kSecondB.y); });
        step = 2;
        break;
    case 2:
        if (!settled()) break;
        subRef = secondSize();
        logNotice("appSetSizeTest") << "reference: sub->setSize(" << str(kSecondB) << ") from its own window -> second "
                                    << str(subRef);
        sub->jobs.push_back([this] { sub->setSize((float)kSecondA.x, (float)kSecondA.y); });   // restore
        step = 3;
        break;
    case 3:
        if (!settled()) break;
        mainBefore = mainSize();
        secondBefore = secondSize();
        // S1: the same call, made from the MAIN window's context.
        sub->setSize((float)kSecondB.x, (float)kSecondB.y);
        step = 4;
        break;
    case 4:
        if (!settled()) break;
        check("S1", same(secondSize(), subRef),
              "sub->setSize() from the MAIN window resizes the second window (got " + str(secondSize()) +
              ", own-context reference " + str(subRef) + ")");
        check("S2", same(mainSize(), mainBefore),
              "...and leaves the main window alone (" + str(mainBefore) + " -> " + str(mainSize()) + ")");
        sub->jobs.push_back([this] { sub->setSize((float)kSecondA.x, (float)kSecondA.y); });   // restore
        step = 5;
        break;
    case 5:
        if (!settled()) break;
        // Reference: the main App resizes itself from its own context.
        setSize((float)mainStart.x + 40, (float)mainStart.y + 30);
        step = 6;
        break;
    case 6:
        if (!settled()) break;
        mainRef = mainSize();
        logNotice("appSetSizeTest") << "reference: main->setSize(+40,+30) from the main window -> main " << str(mainRef);
        if (same(mainRef, mainStart)) {
            logNotice("appSetSizeTest") << "NOTE: the main window did not resize at all on this platform "
                                           "(Linux: setWindowSizeLogical is not implemented). S3 then only "
                                           "shows the call was not misrouted; S4 still checks the second window.";
        }
        setSize((float)mainStart.x, (float)mainStart.y);   // restore
        step = 7;
        break;
    case 7:
        if (!settled()) break;
        mainBefore = mainSize();
        secondBefore = secondSize();
        // S3/S4: the main App's setSize, called from the SECOND window's context.
        sub->jobs.push_back([this] { setSize((float)mainStart.x + 40, (float)mainStart.y + 30); });
        step = 8;
        break;
    case 8:
        if (!settled()) break;
        check("S3", same(mainSize(), mainRef),
              "main->setSize() from the SECOND window resizes the main window (got " + str(mainSize()) +
              ", own-context reference " + str(mainRef) + ")");
        check("S4", same(secondSize(), secondBefore),
              "...and leaves the second window alone (" + str(secondBefore) + " -> " + str(secondSize()) + ")");
        setSize((float)mainStart.x, (float)mainStart.y);   // restore
        sub->jobs.push_back([this] { sub->setSize((float)kSecondA.x, (float)kSecondA.y); });   // restore
        step = 9;
        break;
    case 9:
        if (!settled()) break;
        mainBefore = mainSize();
        secondBefore = secondSize();
        // S5: an App attached to NO window, resized from the second window's
        // context (where a misrouted call is visible even on Linux).
        loose = make_shared<SubApp>();
        sub->jobs.push_back([this] { loose->setSize(300, 200); });
        step = 10;
        break;
    case 10:
        if (!settled()) break;
        check("S5", same(secondSize(), secondBefore) && same(mainSize(), mainBefore),
              "unattached App's setSize() resizes no window (second " + str(secondBefore) + " -> " +
              str(secondSize()) + ", main " + str(mainBefore) + " -> " + str(mainSize()) + ")");
        check("S6", (int)loose->getWidth() == 300 && (int)loose->getHeight() == 200,
              "...but still sets its own size as a Node (" + toString((int)loose->getWidth()) + "x" +
              toString((int)loose->getHeight()) + ")");
        // Now attach it: a setSize() made BEFORE setApp() does not size the
        // window; the window's size wins and is pushed onto the App.
        secondBefore = secondSize();
        second->setApp(loose);
        step = 11;
        break;
    case 11:
        if (!settled()) break;
        check("S7", same(secondSize(), secondBefore),
              "setSize() before setApp() does not resize the window it is attached to later (" +
              str(secondBefore) + " -> " + str(secondSize()) + ")");
        check("S8", (int)loose->getWidth() == secondSize().x && (int)loose->getHeight() == secondSize().y,
              "...the App takes the window's size instead (App " + toString((int)loose->getWidth()) + "x" +
              toString((int)loose->getHeight()) + ", window " + str(secondSize()) + ")");
        step = 99;
        break;
    case 99:
        logNotice("appSetSizeTest") << "RESULT: " << passed << " passed, " << failed << " failed";
        step = 100;
        break;
    case 100:
        if (stepFrames > 60) exitApp();
        break;
    }

    if (step != prevStep) { stepFrames = 0; stableFrames = 0; }
}

void tcApp::draw() {
    clear(0.12f);
    setColor(1.0f);
    drawBitmapString("appSetSizeTest  (runs by itself, then quits)", 20, 30);
    float y = 60;
    for (auto& l : lines) {
        setColor(l.rfind("[PASS]", 0) == 0 ? Color(0.4f, 1.0f, 0.5f) : Color(1.0f, 0.4f, 0.4f));
        drawBitmapString(l, 20, y);
        y += 18;
    }
}
