// =============================================================================
// uiExample - UI コンポーネントのサンプル実装
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // -------------------------------------------------------------------------
    // ボタン1: クリックカウンター
    // -------------------------------------------------------------------------
    button1_ = make_shared<UIButton>();
    button1_->label = "Click Me!";
    button1_->x = 50;
    button1_->y = 50;
    button1_->width = 140;
    button1_->height = 45;
    button1_->onClick = [this]() {
        clickCount_++;
        button1_->label = "Clicked: " + to_string(clickCount_);
        cout << "Button1 clicked! Count: " << clickCount_ << endl;
    };
    addChild(button1_);

    // -------------------------------------------------------------------------
    // ボタン2: 背景色リセット
    // -------------------------------------------------------------------------
    button2_ = make_shared<UIButton>();
    button2_->label = "Reset BG";
    button2_->x = 50;
    button2_->y = 110;
    button2_->width = 140;
    button2_->height = 45;
    button2_->normalColor = Color(0.3f, 0.25f, 0.25f);
    button2_->hoverColor = Color(0.45f, 0.35f, 0.35f);
    button2_->pressColor = Color(0.2f, 0.15f, 0.15f);
    button2_->onClick = [this]() {
        bgColor_ = Color(0.1f, 0.1f, 0.12f);
        slider1_->setValue(0.1f);
        slider2_->setValue(0.1f);
        cout << "Background reset!" << endl;
    };
    addChild(button2_);

    // -------------------------------------------------------------------------
    // スライダー1: 背景色R成分
    // -------------------------------------------------------------------------
    slider1_ = make_shared<UISlider>();
    slider1_->label = "BG Red";
    slider1_->x = 50;
    slider1_->y = 200;
    slider1_->width = 250;
    slider1_->minValue = 0.0f;
    slider1_->maxValue = 0.5f;
    slider1_->setValue(bgColor_.r);
    slider1_->onValueChanged = [this](float v) {
        bgColor_.r = v;
    };
    addChild(slider1_);

    // -------------------------------------------------------------------------
    // スライダー2: 背景色G成分
    // -------------------------------------------------------------------------
    slider2_ = make_shared<UISlider>();
    slider2_->label = "BG Green";
    slider2_->x = 50;
    slider2_->y = 260;
    slider2_->width = 250;
    slider2_->minValue = 0.0f;
    slider2_->maxValue = 0.5f;
    slider2_->setValue(bgColor_.g);
    slider2_->onValueChanged = [this](float v) {
        bgColor_.g = v;
    };
    addChild(slider2_);

    // -------------------------------------------------------------------------
    // スクロールボックス: アイテムリスト
    // -------------------------------------------------------------------------
    scrollBox_ = make_shared<UIScrollBox>();
    scrollBox_->x = 350;
    scrollBox_->y = 50;
    scrollBox_->width = 250;
    scrollBox_->height = 200;
    scrollBox_->contentHeight = 300;  // 10アイテム x 30px
    addChild(scrollBox_);

    cout << "=== uiExample ===" << endl;
    cout << "UI components demo with event handling" << endl;
    cout << "- Click buttons to trigger events" << endl;
    cout << "- Drag sliders or use scroll wheel to change values" << endl;
    cout << "- Scroll inside the box to see items" << endl;
    cout << "- Press R to reset" << endl;
}

void tcApp::update() {
    // 子ノードは自動更新される（updateTree で呼ばれる）
}

void tcApp::draw() {
    // 背景色
    clear(bgColor_);

    // 説明テキスト
    setColor(0.7f, 0.7f, 0.75f);
    drawBitmapString("Buttons: Click to trigger events", 50, 170);
    drawBitmapString("Sliders: Drag or scroll wheel to change value", 50, 310);
    drawBitmapString("ScrollBox: Mouse wheel to scroll content", 350, 270);

    // 現在の背景色を表示
    char buf[128];
    snprintf(buf, sizeof(buf), "Background: R=%.2f G=%.2f B=%.2f",
             bgColor_.r, bgColor_.g, bgColor_.b);
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString(buf, 50, 350);

    // フレームレート
    setColor(0.5f, 0.5f, 0.5f);
    snprintf(buf, sizeof(buf), "FPS: %.1f", getFrameRate());
    drawBitmapString(buf, getWindowWidth() - 100, 30);

    // 子ノード（UIコンポーネント）は自動描画される（drawTree で呼ばれる）
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        // Rキーでリセット
        bgColor_ = Color(0.1f, 0.1f, 0.12f);
        slider1_->setValue(0.1f);
        slider2_->setValue(0.1f);
        clickCount_ = 0;
        button1_->label = "Click Me!";
        cout << "Reset!" << endl;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    // Ray-based Hit Test でイベントを配信
    dispatchMousePress((float)x, (float)y, button);
}

void tcApp::mouseReleased(int x, int y, int button) {
    dispatchMouseRelease((float)x, (float)y, button);
}

void tcApp::mouseDragged(int x, int y, int button) {
    (void)button;  // 未使用
    dispatchMouseMove((float)x, (float)y);
}

void tcApp::mouseScrolled(float dx, float dy) {
    // スクロールイベントを手動で配信
    float mx = getGlobalMouseX();
    float my = getGlobalMouseY();

    Ray globalRay = Ray::fromScreenPoint2D(mx, my);

    // スクロールボックス
    Mat4 globalInv = scrollBox_->getGlobalMatrixInverse();
    Ray localRay = globalRay.transformed(globalInv);
    float dist;
    if (scrollBox_->hitTest(localRay, dist)) {
        scrollBox_->handleScroll(dx, dy);
        return;
    }

    // スライダー2
    globalInv = slider2_->getGlobalMatrixInverse();
    localRay = globalRay.transformed(globalInv);
    if (slider2_->hitTest(localRay, dist)) {
        slider2_->handleScroll(dx, dy);
        return;
    }

    // スライダー1
    globalInv = slider1_->getGlobalMatrixInverse();
    localRay = globalRay.transformed(globalInv);
    if (slider1_->hitTest(localRay, dist)) {
        slider1_->handleScroll(dx, dy);
        return;
    }
}
