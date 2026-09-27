// =============================================================================
// appGetWindowTest - App::getWindow() behavior across attach / detach / close
// =============================================================================
// Runs unattended: opens two secondary windows, attaches / swaps / detaches /
// closes Apps, and checks what getWindow() returns at each point — both from
// the MAIN window's context (subApp->getWindow() called here) and from the
// sub App's OWN context (recorded in SubApp::update()). Every check is logged
// as [PASS] / [FAIL]; the app prints "RESULT: ..." and quits by itself.

#include "tcApp.h"

// --- SubApp ------------------------------------------------------------------

void SubApp::update() {
    updates++;
    seenWindow = getWindow();
    seenCurrentWindow = internal::currentWindow();
}

void SubApp::draw() {
    clear(0.1f, 0.12f, 0.2f);
    setColor(1.0f);
    drawBitmapString(label_ + "  updates=" + toString(updates), 20, 30);
}

// --- tcApp -------------------------------------------------------------------

void tcApp::check(const string& id, bool ok, const string& what) {
    const string line = string(ok ? "[PASS] " : "[FAIL] ") + id + "  " + what;
    (ok ? passed : failed)++;
    lines.push_back(line);
    if (ok) logNotice("appGetWindowTest") << line;
    else    logError("appGetWindowTest") << line;
}

// true once `app` has run update() on its own window since `startUpdates`.
// Fails the step after ~10 s so a window that never ticks cannot hang the run.
bool tcApp::waitForTick(SubApp& app, int startUpdates) {
    if (app.updates > startUpdates) return true;
    if (stepFrames > 600) {
        check("tick", false, app.label_ + " never ran update() on its window");
        step = 99;
    }
    return false;
}

void tcApp::setup() {
    check("T1", getWindow() == nullptr, "main App (runApp) -> nullptr");
    loose = make_shared<SubApp>("loose");
    check("T2", loose->getWindow() == nullptr, "App never attached -> nullptr");
}

void tcApp::update() {
    stepFrames++;
    const int prevStep = step;

    switch (step) {
    case 0: {
        WindowSettings ws;
        ws.setSize(360, 120);
        ws.setTitle("appGetWindowTest - second");
        second = createWindow(ws);
        if (!second) { check("T3", false, "createWindow failed"); step = 99; break; }
        subA = make_shared<SubApp>("subA");
        second->setApp(subA);
        check("T3", subA->getWindow() == second.get(),
              "subA->getWindow() from the MAIN context -> second (not the active window)");
        check("T4", getWindow() == nullptr, "main App still nullptr while a secondary window exists");
        mark = subA->updates;
        step = 1;
        break;
    }
    case 1:
        if (!waitForTick(*subA, mark)) break;
        check("T5", subA->seenWindow == second.get(), "getWindow() inside subA's own update() -> second");
        check("T6", subA->seenCurrentWindow == second.get(),
              "inside its own context, getWindow() agrees with internal::currentWindow()");
        {
            WindowSettings ws;
            ws.setSize(360, 120);
            ws.setTitle("appGetWindowTest - third");
            third = createWindow(ws);
        }
        if (!third) { check("T7", false, "createWindow (third) failed"); step = 99; break; }
        subB = make_shared<SubApp>("subB");
        third->setApp(subB);
        check("T7", subB->getWindow() == third.get() && subA->getWindow() == second.get(),
              "two windows: each App resolves to its own window");
        mark = subB->updates;
        step = 2;
        break;
    case 2:
        if (!waitForTick(*subB, mark)) break;
        check("T8", subB->seenWindow == third.get(), "getWindow() inside subB's own update() -> third");
        // Rejected attach (subB already drives third): nothing may move.
        logNotice("appGetWindowTest") << "expecting one 'already drives another window' error next:";
        second->setApp(subB);
        check("T9", subB->getWindow() == third.get() && subA->getWindow() == second.get(),
              "rejected setApp() leaves both Apps where they were");
        // Swap the App on a live window.
        subC = make_shared<SubApp>("subC");
        second->setApp(subC);
        check("T10", subA->getWindow() == nullptr && subC->getWindow() == second.get(),
              "setApp(other): old App -> nullptr, new App -> second");
        mark = subC->updates;
        step = 3;
        break;
    case 3:
        if (!waitForTick(*subC, mark)) break;
        check("T11", subC->seenWindow == second.get(), "swapped-in App sees second from its own update()");
        second->setApp(nullptr);
        check("T12", subC->getWindow() == nullptr, "setApp(nullptr) detaches -> nullptr");
        second->setApp(subA);
        check("T13", subA->getWindow() == second.get(), "re-attach after detach -> second again");
        step = 4;
        break;
    case 4:
        if (stepFrames < 10) break;   // let the re-attached App tick a little
        second->close();
        check("T14", subA->getWindow() == nullptr && !second->isOpen(),
              "close(): App outlives the window -> nullptr (no dangling pointer)");
        third.reset();
        check("T15", subB->getWindow() == nullptr,
              "Window object destroyed while its App is still held -> nullptr");
        check("T16", getWindow() == nullptr && loose->getWindow() == nullptr,
              "main App and never-attached App still nullptr at the end");
        step = 99;
        break;
    case 99:
        logNotice("appGetWindowTest") << "RESULT: " << passed << " passed, " << failed << " failed";
        step = 100;
        break;
    case 100:
        if (stepFrames > 120) exitApp();
        break;
    }

    if (step != prevStep) stepFrames = 0;
}

void tcApp::draw() {
    clear(0.12f);
    setColor(1.0f);
    drawBitmapString("appGetWindowTest  (runs by itself, then quits)", 20, 30);
    float y = 60;
    for (auto& l : lines) {
        setColor(l.rfind("[PASS]", 0) == 0 ? Color(0.4f, 1.0f, 0.5f) : Color(1.0f, 0.4f, 0.4f));
        drawBitmapString(l, 20, y);
        y += 18;
    }
    if (step >= 99) {
        setColor(1.0f);
        drawBitmapString("RESULT: " + toString(passed) + " passed, " + toString(failed) + " failed", 20, y + 12);
    }
}
