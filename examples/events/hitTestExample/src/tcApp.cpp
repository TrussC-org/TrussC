#include "tcApp.h"

using namespace std;

// ---------------------------------------------------------------------------
// setup - 初期化
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "hitTestExample: Ray-based Hit Test Demo" << endl;
    cout << "  - Click buttons to increment counter" << endl;
    cout << "  - Rotating panel buttons also work!" << endl;
    cout << "  - Press SPACE to pause/resume rotation" << endl;
    cout << "  - Press ESC to quit" << endl;

    // 静止ボタン（左側）- 斜めにオーバーラップさせて、手前だけ反応することを示す
    button1_ = make_shared<CounterButton>();
    button1_->x = 50;
    button1_->y = 150;
    button1_->label = "Back";
    button1_->baseColor = Color(0.4f, 0.2f, 0.2f);
    addChild(button1_);

    button2_ = make_shared<CounterButton>();
    button2_->x = 100;  // 重なるようにずらす
    button2_->y = 180;
    button2_->label = "Middle";
    button2_->baseColor = Color(0.2f, 0.4f, 0.2f);
    addChild(button2_);

    button3_ = make_shared<CounterButton>();
    button3_->x = 150;  // さらに重なるようにずらす
    button3_->y = 210;
    button3_->label = "Front";
    button3_->baseColor = Color(0.2f, 0.2f, 0.4f);
    addChild(button3_);

    // 回転パネル（右側）
    panel_ = make_shared<RotatingPanel>();
    panel_->x = 800;
    panel_->y = 300;
    panel_->width = 350;
    panel_->height = 250;
    addChild(panel_);

    // パネル内のボタン
    panelButton1_ = make_shared<CounterButton>();
    panelButton1_->x = 30;
    panelButton1_->y = 50;
    panelButton1_->label = "Panel Btn1";
    panelButton1_->baseColor = Color(0.5f, 0.3f, 0.1f);
    panel_->addChild(panelButton1_);

    panelButton2_ = make_shared<CounterButton>();
    panelButton2_->x = 30;
    panelButton2_->y = 120;
    panelButton2_->label = "Panel Btn2";
    panelButton2_->baseColor = Color(0.1f, 0.3f, 0.5f);
    panel_->addChild(panelButton2_);
}

// ---------------------------------------------------------------------------
// update - 更新
// ---------------------------------------------------------------------------
void tcApp::update() {
    // 子ノードは自動更新される
}

// ---------------------------------------------------------------------------
// draw - 描画
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.12f);

    // タイトル
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Ray-based Hit Test Demo", 20, 30);

    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Static buttons (left) and rotating panel (right)", 20, 50);
    drawBitmapString("Click works on rotated buttons too!", 20, 65);

    // マウス位置
    char buf[128];
    snprintf(buf, sizeof(buf), "Mouse: %.0f, %.0f", getGlobalMouseX(), getGlobalMouseY());
    setColor(1.0f, 1.0f, 0.5f);
    drawBitmapString(buf, 20, getWindowHeight() - 40);

    // 操作説明
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString("[SPACE] pause/resume  [ESC] quit", 20, getWindowHeight() - 20);

    // パネルの状態
    snprintf(buf, sizeof(buf), "Panel rotation: %.1f deg  %s",
             panel_->rotation * 180.0f / PI,
             paused_ ? "(PAUSED)" : "");
    setColor(0.8f, 0.8f, 0.8f);
    drawBitmapString(buf, 600, 50);

    // 子ノードは自動描画される
}

// ---------------------------------------------------------------------------
// 入力イベント
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == KEY_SPACE) {
        paused_ = !paused_;
        panel_->rotationSpeed = paused_ ? 0.0f : 0.3f;
        cout << "Rotation " << (paused_ ? "paused" : "resumed") << endl;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    // Ray-based Hit Test でイベントを配信
    auto hitNode = dispatchMousePress((float)x, (float)y, button);

    if (hitNode) {
        cout << "Hit node received event" << endl;
    } else {
        cout << "No hit (clicked background)" << endl;
    }
}

void tcApp::mouseReleased(int x, int y, int button) {
    dispatchMouseRelease((float)x, (float)y, button);
}

void tcApp::mouseMoved(int x, int y) {
    // ホバー状態の更新
    // 現在は簡易実装（全ボタンをチェック）
    Ray globalRay = Ray::fromScreenPoint2D((float)x, (float)y);

    // 各ボタンのホバー状態を更新
    auto updateHover = [&](CounterButton::Ptr btn) {
        // ボタンのグローバル逆行列を取得
        Mat4 globalInv = btn->getGlobalMatrixInverse();
        Ray localRay = globalRay.transformed(globalInv);

        float dist;
        btn->isHovered = btn->hitTest(localRay, dist);
    };

    updateHover(button1_);
    updateHover(button2_);
    updateHover(button3_);
    updateHover(panelButton1_);
    updateHover(panelButton2_);
}
