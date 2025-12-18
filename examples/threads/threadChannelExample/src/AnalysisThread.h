#pragma once

#include "TrussC.h"
#include "tc/utils/tcThread.h"
#include "tc/utils/tcThreadChannel.h"
#include <vector>

// =============================================================================
// AnalysisThread - ThreadChannel を使ったワーカースレッドの例
// =============================================================================
//
// oF の threadChannelExample を参考にした実装。
// 2つのチャンネルを使って双方向通信:
//   toAnalyze: メイン → ワーカー（解析リクエスト）
//   analyzed:  ワーカー → メイン（解析結果）
//
// パターン生成データをワーカースレッドで処理し、結果をメインスレッドで描画。
//
class AnalysisThread : public Thread {
public:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 48;
    static constexpr int TOTAL_PIXELS = WIDTH * HEIGHT;

    AnalysisThread() : newFrame_(false) {
        pixels_.resize(TOTAL_PIXELS, 0.0f);
        // クラス生成時にスレッドを開始
        // データが届くまでCPUを使わない
        startThread();
    }

    ~AnalysisThread() {
        // チャンネルを閉じてスレッド終了を待機
        toAnalyze_.close();
        analyzed_.close();
        waitForThread(true);
    }

    // 解析リクエストを送信（メインスレッドから呼ぶ）
    void analyze(const std::vector<float>& pixels) {
        toAnalyze_.send(pixels);
    }

    // 解析結果を受信（メインスレッドから呼ぶ）
    void update() {
        newFrame_ = false;
        // 複数フレームが溜まっていたら最新のものだけ使う
        while (analyzed_.tryReceive(pixels_)) {
            newFrame_ = true;
        }
    }

    // 新しいフレームがあるか
    bool isFrameNew() const {
        return newFrame_;
    }

    // 解析結果を描画
    void draw(float x, float y, float scale = 4.0f) {
        if (pixels_.empty()) {
            setColor(255);
            drawBitmapString("No frames analyzed yet", x + 20, y + 20);
            return;
        }

        for (int py = 0; py < HEIGHT; py++) {
            for (int px = 0; px < WIDTH; px++) {
                int i = py * WIDTH + px;
                float value = pixels_[i];
                setColor(value, value, value);
                drawRect(x + px * scale, y + py * scale, scale, scale);
            }
        }
    }

    // 処理済みフレーム数
    int getAnalyzedCount() const {
        return analyzedCount_;
    }

protected:
    void threadedFunction() override {
        std::vector<float> pixels;

        // receive() はデータが届くまでブロック（CPU使用なし）
        while (toAnalyze_.receive(pixels)) {
            // 解析処理（ここではシンプルな閾値処理）
            for (auto& p : pixels) {
                if (p > 0.5f) {
                    p = 1.0f;
                } else {
                    p = 0.0f;
                }
            }

            analyzedCount_++;

            // 結果をメインスレッドに送信（ムーブで効率化）
            analyzed_.send(std::move(pixels));
        }
    }

private:
    ThreadChannel<std::vector<float>> toAnalyze_;  // メイン → ワーカー
    ThreadChannel<std::vector<float>> analyzed_;   // ワーカー → メイン
    std::vector<float> pixels_;
    bool newFrame_;
    int analyzedCount_ = 0;
};
