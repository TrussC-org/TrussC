#include "tcApp.h"
#include <iostream>

using namespace std;

// ---------------------------------------------------------------------------
// setup - 初期化
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "02_nodes: Node System Demo" << endl;
    cout << "  - Space: 回転を停止/再開" << endl;
    cout << "  - ESC: 終了" << endl;

    // コンテナ1（左側、時計回り）
    container1_ = make_shared<RotatingContainer>();
    container1_->x = 320;
    container1_->y = 360;
    container1_->rotationSpeed = 0.5f;
    container1_->size = 250;

    // コンテナ2（右側、反時計回り、少し小さめ）
    container2_ = make_shared<RotatingContainer>();
    container2_->x = 960;
    container2_->y = 360;
    container2_->rotationSpeed = -0.3f;
    container2_->size = 200;
    container2_->scaleX = 0.8f;
    container2_->scaleY = 0.8f;

    // マウス追従ノード（各コンテナに1つずつ）
    follower1_ = make_shared<MouseFollower>();
    follower1_->r = 1.0f;
    follower1_->g = 0.3f;
    follower1_->b = 0.5f;
    container1_->addChild(follower1_);

    follower2_ = make_shared<MouseFollower>();
    follower2_->r = 0.3f;
    follower2_->g = 1.0f;
    follower2_->b = 0.5f;
    container2_->addChild(follower2_);

    // 固定位置の子ノード（四隅に配置）
    float offset = 80;
    vector<pair<float, float>> positions = {
        {-offset, -offset}, {offset, -offset},
        {-offset, offset}, {offset, offset}
    };

    for (int i = 0; i < 4; i++) {
        auto child = make_shared<FixedChild>();
        child->x = positions[i].first;
        child->y = positions[i].second;
        child->hue = i * tc::QUARTER_TAU;
        container1_->addChild(child);
    }

    for (int i = 0; i < 4; i++) {
        auto child = make_shared<FixedChild>();
        child->x = positions[i].first;
        child->y = positions[i].second;
        child->hue = i * tc::QUARTER_TAU + tc::HALF_TAU;
        child->size = 20;
        container2_->addChild(child);
    }

    // ルート（App）に追加
    addChild(container1_);
    addChild(container2_);
}

// ---------------------------------------------------------------------------
// update - 更新
// ---------------------------------------------------------------------------
void tcApp::update() {
    // App 自身の更新処理があればここに
    // 子ノードはフレームワークが自動で更新する
}

// ---------------------------------------------------------------------------
// draw - 描画
// ---------------------------------------------------------------------------
void tcApp::draw() {
    // 背景クリア
    tc::clear(0.1f, 0.1f, 0.15f);

    // グローバル座標でのマウス位置を表示
    float gx = tc::getGlobalMouseX();
    float gy = tc::getGlobalMouseY();
    tc::setColor(1.0f, 1.0f, 1.0f, 0.5f);
    tc::drawCircle(gx, gy, 5);

    // 画面左上に説明を表示
    tc::setColor(1.0f, 1.0f, 1.0f);
    tc::drawBitmapString("Node System Demo - Local Coordinate Transformation", 20, 25);
    tc::setColor(0.7f, 0.7f, 0.7f);
    tc::drawBitmapString("Each box has its own local coordinate system.", 20, 45);
    tc::drawBitmapString("Mouse position is transformed to local coords.", 20, 60);

    // グローバルマウス座標
    char buf[64];
    snprintf(buf, sizeof(buf), "global: %.0f, %.0f", gx, gy);
    tc::setColor(1.0f, 1.0f, 0.5f);
    tc::drawBitmapString(buf, 20, 90);

    // 操作説明
    tc::setColor(0.5f, 0.5f, 0.5f);
    tc::drawBitmapString("[SPACE] pause/resume rotation  [ESC] quit", 20, tc::getWindowHeight() - 20);

    // 子ノードはフレームワークが自動で描画する（この draw() の後に）
}

// ---------------------------------------------------------------------------
// 入力イベント
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    if (key == tc::KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == tc::KEY_SPACE) {
        // 回転を停止/再開
        static bool paused = false;
        paused = !paused;

        container1_->rotationSpeed = paused ? 0.0f : 0.5f;
        container2_->rotationSpeed = paused ? 0.0f : -0.3f;

        cout << "Rotation " << (paused ? "paused" : "resumed") << endl;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    cout << "Global mouse: " << x << ", " << y << endl;

    // 各フォロワーのローカル座標を表示
    cout << "  Follower1 local: " << follower1_->getMouseX()
         << ", " << follower1_->getMouseY() << endl;
    cout << "  Follower2 local: " << follower2_->getMouseX()
         << ", " << follower2_->getMouseY() << endl;
}

void tcApp::mouseDragged(int x, int y, int button) {
    (void)x; (void)y; (void)button;
}
