#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// Secondary-window content. Records what getWindow() returns from inside its
// OWN window context (update runs on that window's tick).
class SubApp : public App {
public:
    explicit SubApp(string label) : label_(std::move(label)) {}

    void update() override;
    void draw() override;

    string label_;
    int updates = 0;
    Window* seenWindow = nullptr;          // getWindow() inside update()
    Window* seenCurrentWindow = nullptr;   // internal::currentWindow() inside update()
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    void check(const string& id, bool ok, const string& what);
    bool waitForTick(SubApp& app, int startUpdates);

    int step = 0;
    int stepFrames = 0;
    int mark = 0;

    shared_ptr<Window> second, third;
    shared_ptr<SubApp> subA, subB, subC, loose;

    int passed = 0, failed = 0;
    vector<string> lines;
};
