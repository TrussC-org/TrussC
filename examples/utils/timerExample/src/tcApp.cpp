#include "tcApp.h"

// =============================================================================
// TimerBall implementation
// =============================================================================

TimerBall::TimerBall(float x, float y, float radius)
    : radius_(radius)
    , color_(colors::white)
{
    setPos(x, y);
}

void TimerBall::setup() {
    // Change color randomly every 0.5 seconds
    callEvery(0.5, [this]() {
        color_ = colorFromHSB(random(TAU), 0.8f, 1.0f);
        colorChangeCount_++;
    });
}

void TimerBall::draw() {
    setColor(color_);
    drawCircle(0, 0, radius_);

    // Display change count
    setColor(0.0f);
    string countStr = to_string(colorChangeCount_);
    drawBitmapString(countStr, -4, 4);
}

// =============================================================================
// CountdownNode implementation
// =============================================================================

CountdownNode::CountdownNode() {
    setPos(50, 50);
}

void CountdownNode::setup() {
    // Change message once after 3 seconds
    callAfter(3.0, [this]() {
        message_ = "Executed by callAfter!";
        triggered_ = true;
        tcLogNotice("Timer") << "callAfter triggered!";
    });
}

void CountdownNode::draw() {
    Color bgColor = triggered_ ? Color(0.2f, 0.6f, 0.2f) : Color(0.3f, 0.3f, 0.3f);
    Color textColor = colors::white;

    drawBitmapStringHighlight(message_, 0, 0, bgColor, textColor);
}

// =============================================================================
// PulseNode implementation
// =============================================================================

PulseNode::PulseNode() {
    setPos(400, 450);
}

void PulseNode::setup() {
    // Pulse every 0.3 seconds
    pulseTimerId_ = callEvery(0.3, [this]() {
        pulseScale_ = 1.5f;  // Start pulse
        pulseCount_++;

        // Cancel timer after 10 pulses
        if (pulseCount_ >= 10) {
            cancelTimer(pulseTimerId_);
            tcLogNotice("Timer") << "Pulse timer cancelled after 10 pulses";
        }
    });
}

void PulseNode::draw() {
    // Slowly return pulse to normal
    pulseScale_ = tc::lerp(pulseScale_, 1.0f, 0.1f);

    // Pulsing rectangle
    float size = 60.0f * pulseScale_;
    Color c = (pulseCount_ >= 10) ? colors::gray : colors::coral;
    setColor(c);
    drawRect(-size / 2, -size / 2, size, size);

    // Display pulse count
    setColor(1.0f);
    string info = "Pulse: " + to_string(pulseCount_) + "/10";
    drawBitmapString(info, -40, size / 2 + 15);

    if (pulseCount_ >= 10) {
        drawBitmapString("(Timer cancelled)", -55, size / 2 + 30);
    }
}

// =============================================================================
// tcApp implementation
// =============================================================================

void tcApp::setup() {
    tcLogNotice("tcApp") << "timerExample: callAfter / callEvery Demo";
    tcLogNotice("tcApp") << "  - Press R to reset all timers";

    // Countdown node
    countdownNode_ = make_shared<CountdownNode>();
    addChild(countdownNode_);

    // Timer balls (3 balls)
    float startX = 150;
    float spacing = 150;
    for (int i = 0; i < 3; i++) {
        auto ball = make_shared<TimerBall>(startX + i * spacing, 200, 40);
        addChild(ball);
        balls_.push_back(ball);
    }

    // Pulse node
    pulseNode_ = make_shared<PulseNode>();
    addChild(pulseNode_);
}

void tcApp::update() {
    // Child nodes are updated automatically by App
}

void tcApp::draw() {
    clear(0.12f, 0.12f, 0.16f);

    // Title
    setColor(1.0f);
    drawBitmapStringHighlight("timerExample - callAfter / callEvery Demo",
        10, 20, Color(0, 0, 0, 0.7f), colors::white);

    // Description
    drawBitmapStringHighlight("callAfter: Execute once after 3 seconds",
        50, 80, Color(0, 0, 0, 0.5f), colors::lightGray);

    drawBitmapStringHighlight("callEvery: Color changes every 0.5 seconds",
        100, 140, Color(0, 0, 0, 0.5f), colors::lightGray);

    drawBitmapStringHighlight("callEvery + cancelTimer: Timer stops after 10 pulses",
        200, 380, Color(0, 0, 0, 0.5f), colors::lightGray);

    // Child nodes are drawn automatically by App

    // Control instructions
    drawBitmapStringHighlight("Press R to reset",
        10, getWindowHeight() - 20, Color(0, 0, 0, 0.7f), colors::white);
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        // Reset: recreate nodes
        removeAllChildren();
        balls_.clear();

        // Re-setup (setup() is called automatically on first update)
        countdownNode_ = make_shared<CountdownNode>();
        addChild(countdownNode_);

        for (int i = 0; i < 3; i++) {
            auto ball = make_shared<TimerBall>(150 + i * 150, 200, 40);
            addChild(ball);
            balls_.push_back(ball);
        }

        pulseNode_ = make_shared<PulseNode>();
        addChild(pulseNode_);

        tcLogNotice("tcApp") << "Reset all timers";
    }
}
