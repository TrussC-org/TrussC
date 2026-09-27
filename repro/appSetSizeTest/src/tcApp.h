#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// Secondary-window content. Runs queued jobs inside its OWN window context
// (update() is called on that window's tick), so the test can issue calls
// "from the second window" as well as from the main one.
class SubApp : public App {
public:
    void update() override;
    void draw() override;

    vector<function<void()>> jobs;
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    void check(const string& id, bool ok, const string& what);
    IVec2 mainSize() const;     // main window, read from the main context
    IVec2 secondSize() const;   // second window, read from its Window handle
    bool settled();             // true once both sizes held still for a while

    int step = 0;
    int stepFrames = 0;
    int stableFrames = 0;
    IVec2 lastMain, lastSecond;

    shared_ptr<Window> second;
    shared_ptr<SubApp> sub, loose;

    IVec2 mainStart, secondStart;   // sizes before any resize
    IVec2 subRef, mainRef;          // results of the own-context reference calls
    IVec2 mainBefore, secondBefore; // snapshot right before a cross-window call

    int passed = 0, failed = 0;
    vector<string> lines;
};
