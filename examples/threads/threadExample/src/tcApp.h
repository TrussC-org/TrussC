#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "threadedObject.h"

// =============================================================================
// tcApp - スレッドサンプルアプリケーション
// =============================================================================
//
// oF の threadExample を参考にした実装。
// スレッドでデータを生成し、メインスレッドで描画する。
//
// 操作:
//   a: スレッド開始
//   s: スレッド停止
//   l: ロックあり更新に切り替え
//   n: ロックなし更新に切り替え（ティアリング発生）
//
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;

    // スレッドオブジェクト
    ThreadedObject threadedObject;

    // ロックを使うかどうか
    bool doLock = false;
};
