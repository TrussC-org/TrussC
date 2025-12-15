#pragma once

#include "TrussC.h"
#include "tc/utils/tcThread.h"
#include <condition_variable>
#include <vector>
#include <cmath>

// =============================================================================
// ThreadedObject - スレッドで計算を行うオブジェクト
// tc::Thread を継承して使用する例
// =============================================================================
//
// oF の threadExample を参考にした実装。
// スレッドでピクセルデータ（ノイズパターン）を生成し、
// メインスレッドで描画用データとして取得する。
//
// Mutex を使ったデータ保護の重要性をデモするため、
// update() と updateNoLock() の両方を提供。
//
class ThreadedObject : public tc::Thread {
public:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 48;
    static constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    // デストラクタ - スレッド終了を待機
    ~ThreadedObject() {
        stop();
        waitForThread(false);
    }

    // 初期化
    void setup() {
        pixelData.resize(TOTAL_PIXELS);
        displayData.resize(TOTAL_PIXELS);
        for (int i = 0; i < TOTAL_PIXELS; i++) {
            pixelData[i] = 0.0f;
            displayData[i] = 0.0f;
        }
        start();
    }

    // スレッド開始
    void start() {
        startThread();
    }

    // スレッド停止
    // condition_variable に通知して、スレッドのwaitを解除
    void stop() {
        std::unique_lock<std::mutex> lck(mutex);
        stopThread();
        condition.notify_all();
    }

    // スレッドで実行される処理
    void threadedFunction() override {
        printf("[threadedFunction] thread started\n");

        while (isThreadRunning()) {
            // スレッドフレーム数をインクリメント
            threadFrameNum++;

            // ピクセルデータを更新（ロックしてから）
            {
                std::unique_lock<std::mutex> lock(mutex);

                // シンプルなパターン生成（sin波 + 時間変化）
                float t = threadFrameNum * 0.05f;
                for (int i = 0; i < TOTAL_PIXELS; i++) {
                    float ux = (i % WIDTH) / (float)WIDTH;
                    float uy = (i / WIDTH) / (float)HEIGHT;

                    // シンプルなノイズ的パターン
                    float value = std::sin(ux * 10.0f + t) * std::sin(uy * 10.0f + t * 0.7f);
                    value = (value + 1.0f) * 0.5f;  // 0-1 に正規化
                    pixelData[i] = value;
                }

                // メインスレッドがデータを取得するまで待機
                // ただし、停止シグナルが来たら抜ける
                condition.wait(lock, [this]{ return !isThreadRunning() || dataReady; });
                dataReady = false;
            }
        }

        printf("[threadedFunction] thread stopped\n");
    }

    // 更新（ロックあり）- ティアリングなし
    void update() {
        std::unique_lock<std::mutex> lock(mutex);
        // ピクセルデータを描画用バッファにコピー
        displayData = pixelData;
        dataReady = true;
        condition.notify_all();
    }

    // 更新（ロックなし）- ティアリングあり
    void updateNoLock() {
        // ロックしないのでティアリングが発生する可能性
        displayData = pixelData;
        dataReady = true;
        condition.notify_all();
    }

    // 描画
    void draw(float x, float y, float scale = 4.0f) {
        // displayData を使って矩形を描画
        for (int py = 0; py < HEIGHT; py++) {
            for (int px = 0; px < WIDTH; px++) {
                int i = py * WIDTH + px;
                float value = displayData[i];
                tc::setColor(value, value, value);
                tc::drawRect(x + px * scale, y + py * scale, scale, scale);
            }
        }
    }

    // スレッドフレーム数を取得
    int getThreadFrameNum() const {
        return threadFrameNum;
    }

protected:
    // 計算用ピクセルデータ（スレッドで更新）
    std::vector<float> pixelData;

    // 描画用ピクセルデータ（メインスレッドで使用）
    std::vector<float> displayData;

    // 同期用
    std::condition_variable condition;

    // データ準備フラグ（スプリアスウェイクアップ対策）
    bool dataReady = false;

    // スレッドのフレーム数
    int threadFrameNum = 0;
};
