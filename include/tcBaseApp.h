#pragma once

#include "tcNode.h"

// =============================================================================
// trussc 名前空間
// =============================================================================
namespace trussc {

// =============================================================================
// App - アプリケーション基底クラス
// tc::Node を継承し、シーングラフのルートノードとして機能する
// このクラスを継承して tcApp を作成する
// =============================================================================

class App : public Node {
public:
    virtual ~App() = default;

    // -------------------------------------------------------------------------
    // ライフサイクル（Node から継承、追加のオーバーライド可能）
    // -------------------------------------------------------------------------

    // setup(), update(), draw(), cleanup() は Node から継承

    // -------------------------------------------------------------------------
    // キーボードイベント（従来のスタイル）
    // -------------------------------------------------------------------------

    virtual void keyPressed(int key) {
        (void)key;
    }

    virtual void keyReleased(int key) {
        (void)key;
    }

    // -------------------------------------------------------------------------
    // マウスイベント（従来のスタイル - 画面座標で届く）
    // -------------------------------------------------------------------------

    virtual void mousePressed(int x, int y, int button) {
        (void)x;
        (void)y;
        (void)button;
    }

    virtual void mouseReleased(int x, int y, int button) {
        (void)x;
        (void)y;
        (void)button;
    }

    virtual void mouseMoved(int x, int y) {
        (void)x;
        (void)y;
    }

    virtual void mouseDragged(int x, int y, int button) {
        (void)x;
        (void)y;
        (void)button;
    }

    virtual void mouseScrolled(float deltaX, float deltaY) {
        (void)deltaX;
        (void)deltaY;
    }

    // -------------------------------------------------------------------------
    // ウィンドウイベント
    // -------------------------------------------------------------------------

    virtual void windowResized(int width, int height) {
        (void)width;
        (void)height;
    }
};

} // namespace trussc
