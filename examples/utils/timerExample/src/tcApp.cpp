#include "tcApp.h"

// =============================================================================
// TimerBall 実装
// =============================================================================

TimerBall::TimerBall(float x, float y, float radius)
    : radius_(radius)
    , color_(colors::white)
{
    this->x = x;
    this->y = y;
}

void TimerBall::setup() {
    // 0.5秒ごとに色をランダムに変更
    callEvery(0.5, [this]() {
        color_ = colorFromHSB(random(TAU), 0.8f, 1.0f);
        colorChangeCount_++;
    });
}

void TimerBall::draw() {
    setColor(color_);
    drawCircle(0, 0, radius_);

    // 変更回数を表示
    setColor(0);
    string countStr = to_string(colorChangeCount_);
    drawBitmapString(countStr, -4, 4);
}

// =============================================================================
// CountdownNode 実装
// =============================================================================

CountdownNode::CountdownNode() {
    x = 50;
    y = 50;
}

void CountdownNode::setup() {
    // 3秒後に1回だけメッセージを変更
    callAfter(3.0, [this]() {
        message_ = "callAfter で実行されました！";
        triggered_ = true;
        cout << "callAfter triggered!" << endl;
    });
}

void CountdownNode::draw() {
    Color bgColor = triggered_ ? Color(0.2f, 0.6f, 0.2f) : Color(0.3f, 0.3f, 0.3f);
    Color textColor = colors::white;

    drawBitmapStringHighlight(message_, 0, 0, bgColor, textColor);
}

// =============================================================================
// PulseNode 実装
// =============================================================================

PulseNode::PulseNode() {
    x = 400;
    y = 450;
}

void PulseNode::setup() {
    // 0.3秒ごとにパルス
    pulseTimerId_ = callEvery(0.3, [this]() {
        pulseScale_ = 1.5f;  // パルス開始
        pulseCount_++;

        // 10回パルスしたらタイマーをキャンセル
        if (pulseCount_ >= 10) {
            cancelTimer(pulseTimerId_);
            cout << "Pulse timer cancelled after 10 pulses" << endl;
        }
    });
}

void PulseNode::draw() {
    // パルスをゆっくり戻す
    pulseScale_ = lerp(pulseScale_, 1.0f, 0.1f);

    // パルスする四角形
    float size = 60.0f * pulseScale_;
    Color c = (pulseCount_ >= 10) ? colors::gray : colors::coral;
    setColor(c);
    drawRect(-size / 2, -size / 2, size, size);

    // パルス回数を表示
    setColor(255);
    string info = "Pulse: " + to_string(pulseCount_) + "/10";
    drawBitmapString(info, -40, size / 2 + 15);

    if (pulseCount_ >= 10) {
        drawBitmapString("(Timer cancelled)", -55, size / 2 + 30);
    }
}

// =============================================================================
// tcApp 実装
// =============================================================================

void tcApp::setup() {
    cout << "timerExample: callAfter / callEvery Demo" << endl;
    cout << "  - Press R to reset all timers" << endl;

    // ルートノードを作成
    rootNode_ = make_shared<Node>();

    // カウントダウンノード
    countdownNode_ = make_shared<CountdownNode>();
    rootNode_->addChild(countdownNode_);
    countdownNode_->setup();

    // タイマーボール（3つ）
    float startX = 150;
    float spacing = 150;
    for (int i = 0; i < 3; i++) {
        auto ball = make_shared<TimerBall>(startX + i * spacing, 200, 40);
        rootNode_->addChild(ball);
        ball->setup();
        balls_.push_back(ball);
    }

    // パルスノード
    pulseNode_ = make_shared<PulseNode>();
    rootNode_->addChild(pulseNode_);
    pulseNode_->setup();
}

void tcApp::update() {
    // ノードツリーを更新（タイマーもここで処理される）
    rootNode_->updateTree();
}

void tcApp::draw() {
    clear(30, 30, 40);

    // タイトル
    setColor(255);
    drawBitmapStringHighlight("timerExample - callAfter / callEvery Demo",
        10, 20, Color(0, 0, 0, 0.7f), colors::white);

    // 説明
    drawBitmapStringHighlight("callAfter: 3秒後に1回だけ実行",
        50, 80, Color(0, 0, 0, 0.5f), colors::lightGray);

    drawBitmapStringHighlight("callEvery: 0.5秒ごとに色が変わる",
        100, 140, Color(0, 0, 0, 0.5f), colors::lightGray);

    drawBitmapStringHighlight("callEvery + cancelTimer: 10回でタイマー停止",
        200, 380, Color(0, 0, 0, 0.5f), colors::lightGray);

    // ノードツリーを描画
    rootNode_->drawTree();

    // 操作説明
    drawBitmapStringHighlight("Press R to reset",
        10, getWindowHeight() - 20, Color(0, 0, 0, 0.7f), colors::white);
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        // リセット: ノードを再作成
        rootNode_->removeAllChildren();
        balls_.clear();

        // 再セットアップ
        countdownNode_ = make_shared<CountdownNode>();
        rootNode_->addChild(countdownNode_);
        countdownNode_->setup();

        for (int i = 0; i < 3; i++) {
            auto ball = make_shared<TimerBall>(150 + i * 150, 200, 40);
            rootNode_->addChild(ball);
            ball->setup();
            balls_.push_back(ball);
        }

        pulseNode_ = make_shared<PulseNode>();
        rootNode_->addChild(pulseNode_);
        pulseNode_->setup();

        cout << "Reset all timers" << endl;
    }
}
