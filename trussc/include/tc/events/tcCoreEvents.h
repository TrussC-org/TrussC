#pragma once

// =============================================================================
// tcCoreEvents - フレームワークのコアイベント集
// =============================================================================

#include "tcEvent.h"
#include "tcEventArgs.h"

namespace trussc {

// ---------------------------------------------------------------------------
// CoreEvents - フレームワークのコアイベント
// ---------------------------------------------------------------------------
class CoreEvents {
public:
    // アプリライフサイクル
    Event<void> setup;            // setup完了後
    Event<void> update;           // 毎フレーム update 前
    Event<void> draw;             // 毎フレーム draw 前
    Event<void> exit;             // アプリ終了時

    // キーボード
    Event<KeyEventArgs> keyPressed;
    Event<KeyEventArgs> keyReleased;

    // マウス
    Event<MouseEventArgs> mousePressed;
    Event<MouseEventArgs> mouseReleased;
    Event<MouseMoveEventArgs> mouseMoved;
    Event<MouseDragEventArgs> mouseDragged;
    Event<ScrollEventArgs> mouseScrolled;

    // ウィンドウ
    Event<ResizeEventArgs> windowResized;

    // ドラッグ&ドロップ
    Event<DragDropEventArgs> filesDropped;

    // コンソール入力（stdin からのコマンド）
    Event<ConsoleEventArgs> console;

    // 将来用
    // Event<TouchEventArgs> touchBegan;
    // Event<TouchEventArgs> touchMoved;
    // Event<TouchEventArgs> touchEnded;
};

// ---------------------------------------------------------------------------
// グローバルアクセサ
// ---------------------------------------------------------------------------
inline CoreEvents& events() {
    static CoreEvents instance;
    return instance;
}

} // namespace trussc
