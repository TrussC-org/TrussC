#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "tcNode.h"
#include <iostream>
#include <vector>

using namespace std;

// =============================================================================
// timerExample - callAfter / callEvery sample
// =============================================================================
// - callAfter: Execute once after specified seconds
// - callEvery: Execute repeatedly at specified interval
// - cancelTimer: Cancel a timer
// =============================================================================

// ---------------------------------------------------------------------------
// TimerBall - Ball that changes color with timer (inherits from Node)
// ---------------------------------------------------------------------------
class TimerBall : public Node {
public:
    TimerBall(float x, float y, float radius);

    void setup() override;
    void draw() override;

private:
    float radius_;
    Color color_;
    int colorChangeCount_ = 0;
};

// ---------------------------------------------------------------------------
// CountdownNode - Demo of executing once with callAfter
// ---------------------------------------------------------------------------
class CountdownNode : public Node {
public:
    CountdownNode();

    void setup() override;
    void draw() override;

private:
    string message_ = "Message will change after 3 seconds...";
    bool triggered_ = false;
};

// ---------------------------------------------------------------------------
// PulseNode - Demo of repeated execution with callEvery
// ---------------------------------------------------------------------------
class PulseNode : public Node {
public:
    PulseNode();

    void setup() override;
    void draw() override;

private:
    float pulseScale_ = 1.0f;
    int pulseCount_ = 0;
    uint64_t pulseTimerId_ = 0;
};

// ---------------------------------------------------------------------------
// tcApp - Main application
// ---------------------------------------------------------------------------
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Node::Ptr rootNode_;
    vector<shared_ptr<TimerBall>> balls_;
    shared_ptr<CountdownNode> countdownNode_;
    shared_ptr<PulseNode> pulseNode_;
};
