// =============================================================================
// blendingExample - ブレンドモードの比較デモ
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    cout << "=== blendingExample ===" << endl;
    cout << "Blend Mode Comparison Demo" << endl;
    cout << "[1-6] Switch blend mode" << endl;
    cout << "  1: Alpha (default)" << endl;
    cout << "  2: Add" << endl;
    cout << "  3: Multiply" << endl;
    cout << "  4: Screen" << endl;
    cout << "  5: Subtract" << endl;
    cout << "  6: Disabled" << endl;
}

void tcApp::update() {
    animTime_ += tc::getDeltaTime();
}

void tcApp::draw() {
    tc::clear(0.15f, 0.15f, 0.15f);

    float w = tc::getWindowWidth();
    float h = tc::getWindowHeight();

    // タイトル
    tc::setColor(1.0f, 1.0f, 1.0f);
    tc::drawBitmapString("Blend Mode Comparison", 20, 30);
    tc::setColor(0.7f, 0.7f, 0.7f);
    tc::drawBitmapString("Press 1-6 to switch modes, each column shows different blend mode", 20, 50);

    // 各ブレンドモードの説明
    tc::BlendMode modes[] = {
        tc::BlendMode::Alpha,
        tc::BlendMode::Add,
        tc::BlendMode::Multiply,
        tc::BlendMode::Screen,
        tc::BlendMode::Subtract,
        tc::BlendMode::Disabled
    };

    int numModes = 6;
    float colWidth = w / numModes;
    float startY = 100;

    // 背景パターンを描画（各列の下地）
    for (int i = 0; i < numModes; i++) {
        float x = i * colWidth;

        // 背景グラデーション（暗い→明るい）
        tc::setBlendMode(tc::BlendMode::Disabled);  // 背景は上書きで描画
        for (int j = 0; j < 10; j++) {
            float gray = 0.1f + j * 0.08f;
            tc::setColor(gray, gray, gray);
            tc::drawRect(x, startY + j * 50, colWidth - 10, 50);
        }

        // カラフルな背景要素
        tc::setBlendMode(tc::BlendMode::Alpha);
        tc::setColor(0.8f, 0.2f, 0.2f, 0.7f);  // 赤
        tc::drawRect(x + 10, startY + 100, 60, 60);
        tc::setColor(0.2f, 0.8f, 0.2f, 0.7f);  // 緑
        tc::drawRect(x + 40, startY + 140, 60, 60);
        tc::setColor(0.2f, 0.2f, 0.8f, 0.7f);  // 青
        tc::drawRect(x + 70, startY + 180, 60, 60);
    }

    // 各モードで円を描画
    for (int i = 0; i < numModes; i++) {
        float x = i * colWidth + colWidth / 2;

        tc::setBlendMode(modes[i]);

        // モード名ラベル
        tc::setBlendMode(tc::BlendMode::Alpha);  // テキストは常に Alpha
        tc::setColor(1.0f, 1.0f, 1.0f);
        tc::drawBitmapString(getBlendModeName(modes[i]), i * colWidth + 10, startY - 10);

        // 再度ブレンドモードを設定
        tc::setBlendMode(modes[i]);

        // アニメーションする円（半透明）
        float anim = sin(animTime_ * 2.0f + i * 0.5f) * 0.5f + 0.5f;

        // 白い円（alpha 0.7）
        tc::setColor(1.0f, 1.0f, 1.0f, 0.7f);
        tc::drawCircle(x, startY + 150 + anim * 50, 50);

        // 赤い円
        tc::setColor(1.0f, 0.3f, 0.3f, 0.7f);
        tc::drawCircle(x - 30, startY + 280, 40);

        // 緑の円
        tc::setColor(0.3f, 1.0f, 0.3f, 0.7f);
        tc::drawCircle(x, startY + 320, 40);

        // 青い円
        tc::setColor(0.3f, 0.3f, 1.0f, 0.7f);
        tc::drawCircle(x + 30, startY + 360, 40);

        // 黄色い円（重なり部分を見るため）
        tc::setColor(1.0f, 1.0f, 0.3f, 0.5f);
        tc::drawCircle(x, startY + 450, 60);
    }

    // デフォルトに戻す
    tc::resetBlendMode();

    // 説明テキスト
    tc::setColor(0.6f, 0.6f, 0.6f);
    tc::drawBitmapString("Alpha: Standard transparency blending", 20, h - 100);
    tc::drawBitmapString("Add: Brightens (good for glow effects)", 20, h - 85);
    tc::drawBitmapString("Multiply: Darkens (good for shadows)", 20, h - 70);
    tc::drawBitmapString("Screen: Brightens (inverse of Multiply)", 20, h - 55);
    tc::drawBitmapString("Subtract: Darkens by subtracting", 20, h - 40);
    tc::drawBitmapString("Disabled: No blending (overwrites)", 20, h - 25);
}

void tcApp::keyPressed(int key) {
    // 現在のモードを即座に切り替えるデモ（画面全体には適用しないが確認用）
    switch (key) {
        case '1':
            cout << "Alpha mode (default)" << endl;
            break;
        case '2':
            cout << "Add mode" << endl;
            break;
        case '3':
            cout << "Multiply mode" << endl;
            break;
        case '4':
            cout << "Screen mode" << endl;
            break;
        case '5':
            cout << "Subtract mode" << endl;
            break;
        case '6':
            cout << "Disabled mode" << endl;
            break;
    }
}

string tcApp::getBlendModeName(tc::BlendMode mode) {
    switch (mode) {
        case tc::BlendMode::Alpha: return "Alpha";
        case tc::BlendMode::Add: return "Add";
        case tc::BlendMode::Multiply: return "Multiply";
        case tc::BlendMode::Screen: return "Screen";
        case tc::BlendMode::Subtract: return "Subtract";
        case tc::BlendMode::Disabled: return "Disabled";
        default: return "Unknown";
    }
}
