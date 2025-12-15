#pragma once

#include "tcBaseApp.h"
#include <vector>

using namespace trussc;

// mouseExample - マウス入力のデモ
// マウス位置、ボタン、ドラッグ、スクロールの可視化

class tcApp : public tc::App {
public:
    void setup() override;
    void draw() override;

    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float dx, float dy) override;

private:
    // ドラッグ軌跡
    struct DragPoint {
        float x, y;
        int button;
    };
    std::vector<DragPoint> dragTrail;

    // クリック位置
    struct ClickPoint {
        float x, y;
        int button;
        float alpha;  // フェードアウト用
    };
    std::vector<ClickPoint> clickPoints;

    // スクロール累積値
    float scrollX = 0;
    float scrollY = 0;

    // 現在のマウス状態
    bool isDragging = false;
    int currentButton = -1;
};
