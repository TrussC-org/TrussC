#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "tcNode.h"
#include <iostream>
#include <vector>

using namespace std;

// =============================================================================
// timerExample - callAfter / callEvery サンプル
// =============================================================================
// - callAfter: 指定秒数後に1回だけ実行
// - callEvery: 指定間隔で繰り返し実行
// - cancelTimer: タイマーをキャンセル
// =============================================================================

// ---------------------------------------------------------------------------
// TimerBall - タイマーで色が変わるボール（Node を継承）
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
// CountdownNode - callAfter で一度だけ実行するデモ
// ---------------------------------------------------------------------------
class CountdownNode : public Node {
public:
    CountdownNode();

    void setup() override;
    void draw() override;

private:
    string message_ = "3秒後にメッセージが変わります...";
    bool triggered_ = false;
};

// ---------------------------------------------------------------------------
// PulseNode - callEvery で繰り返し実行するデモ
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
// tcApp - メインアプリケーション
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
