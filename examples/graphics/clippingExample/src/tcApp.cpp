// =============================================================================
// clippingExample - Scissor Clipping のデモ実装
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    cout << "=== clippingExample ===" << endl;
    cout << "Nested Scissor Clipping Demo" << endl;
    cout << "- Outer box clips inner content" << endl;
    cout << "- Inner box also clips its content" << endl;
    cout << "- Circles only visible in intersection of both boxes" << endl;
    cout << "[R] reset positions" << endl;

    // 外側のクリップボックス
    outerBox_ = make_shared<ClipBox>();
    outerBox_->x = 100;
    outerBox_->y = 100;
    outerBox_->width = 500;
    outerBox_->height = 400;
    outerBox_->bgColor = tc::Color(0.15f, 0.15f, 0.2f);
    outerBox_->borderColor = tc::Color(0.3f, 0.5f, 0.8f);
    outerBox_->label = "Outer ClipBox";
    addChild(outerBox_);

    // 内側のクリップボックス（回転する）
    innerBox_ = make_shared<ClipBox>();
    innerBox_->x = 100;
    innerBox_->y = 80;
    innerBox_->width = 300;
    innerBox_->height = 220;
    innerBox_->bgColor = tc::Color(0.2f, 0.15f, 0.15f);
    innerBox_->borderColor = tc::Color(0.8f, 0.5f, 0.3f);
    innerBox_->label = "Inner ClipBox (rotating)";
    outerBox_->addChild(innerBox_);

    // 跳ね回る円を内側ボックスに追加
    for (int i = 0; i < 5; i++) {
        auto circle = make_shared<BouncingCircle>();
        circle->x = 50 + i * 50;
        circle->y = 50 + i * 30;
        circle->radius = 20 + i * 5;
        circle->vx = 1.5f + i * 0.3f;
        circle->vy = 1.0f + i * 0.4f;
        circle->boundsWidth = innerBox_->width;
        circle->boundsHeight = innerBox_->height;
        circle->color = tc::colorFromHSB(i * 0.15f, 0.7f, 0.9f);
        innerBox_->addChild(circle);
        circles_.push_back(circle);
    }

    // 外側ボックスにも直接円を追加（外側クリッピングのテスト）
    auto outerCircle = make_shared<BouncingCircle>();
    outerCircle->x = 400;
    outerCircle->y = 300;
    outerCircle->radius = 40;
    outerCircle->vx = -1.2f;
    outerCircle->vy = 0.8f;
    outerCircle->boundsWidth = outerBox_->width;
    outerCircle->boundsHeight = outerBox_->height;
    outerCircle->color = tc::Color(0.3f, 0.8f, 0.4f);
    outerBox_->addChild(outerCircle);
    circles_.push_back(outerCircle);
}

void tcApp::update() {
    // 円は自動でupdateTreeで更新される
}

void tcApp::draw() {
    tc::clear(0.08f, 0.08f, 0.1f);

    // タイトル
    tc::setColor(1.0f, 1.0f, 1.0f);
    tc::drawBitmapString("Nested Scissor Clipping Demo", 20, 30);

    tc::setColor(0.7f, 0.7f, 0.7f);
    tc::drawBitmapString("Circles are clipped by BOTH outer and inner boxes", 20, 50);
    tc::drawBitmapString("Inner box circles only visible in intersection area", 20, 65);

    // 右側に説明
    tc::setColor(0.5f, 0.5f, 0.5f);
    tc::drawBitmapString("[R] reset positions", 700, 100);

    // クリッピングの状態表示
    tc::setColor(0.3f, 0.5f, 0.8f);
    tc::drawBitmapString("Blue = Outer clip boundary", 700, 180);
    tc::setColor(0.8f, 0.5f, 0.3f);
    tc::drawBitmapString("Orange = Inner clip boundary", 700, 200);

    // 子ノードは自動描画される
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        // リセット
        for (size_t i = 0; i < circles_.size(); i++) {
            circles_[i]->x = 50 + i * 50;
            circles_[i]->y = 50 + i * 30;
        }
        cout << "Reset" << endl;
    }
}
