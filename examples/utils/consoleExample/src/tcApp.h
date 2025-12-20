#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>
#include <vector>
#include <deque>

using namespace std;

// =============================================================================
// consoleExample - stdin からコマンドを受け取るサンプル
// =============================================================================
// AI アシスタントや外部プロセスからコマンドを送信できる。
//
// 使い方（ターミナルから実行）:
//   ./consoleExample
//   >>> tcdebug info          # アプリ情報を JSON で取得
//   >>> tcdebug screenshot /tmp/shot.png  # スクリーンショット
//   >>> spawn 100 200         # (100, 200) にボールを生成
//   >>> clear                  # ボールをクリア
//
// パイプで外部から送信:
//   echo "spawn 200 300" | ./consoleExample
//
// =============================================================================

// ---------------------------------------------------------------------------
// Ball - シンプルなボール
// ---------------------------------------------------------------------------
struct Ball {
    float x, y;
    Color color;
};

// ---------------------------------------------------------------------------
// tcApp - メインアプリケーション
// ---------------------------------------------------------------------------
class tcApp : public App {
public:
    void setup() override {
        // コンソールイベントをリッスン（リスナーを保持）
        events().console.listen(consoleListener_, [this](ConsoleEventArgs& e) {
            // ログに追加（最新10件）
            commandLog_.push_back(e.raw);
            if (commandLog_.size() > 10) {
                commandLog_.pop_front();
            }

            // カスタムコマンドを処理
            if (e.args.empty()) return;

            if (e.args[0] == "spawn" && e.args.size() >= 3) {
                // spawn x y - ボールを生成
                float x = stof(e.args[1]);
                float y = stof(e.args[2]);
                Ball ball;
                ball.x = x;
                ball.y = y;
                ball.color = ColorHSB(random(TAU), 0.8f, 0.9f).toRGB();
                balls_.push_back(ball);
                cout << "{\"status\":\"ok\",\"command\":\"spawn\",\"ballCount\":" << balls_.size() << "}" << endl;
            }
            else if (e.args[0] == "clear") {
                // clear - ボールをクリア
                balls_.clear();
                cout << "{\"status\":\"ok\",\"command\":\"clear\"}" << endl;
            }
        });

        cout << "consoleExample started. Ready for commands." << endl;
        cout << "Try: tcdebug info, spawn 100 200, clear" << endl;
    }

    void draw() override {
        clear(30);

        // 使い方を表示
        setColor(200);
        drawBitmapString("Console Example - stdin commands", 20, 30);
        drawBitmapString("Commands:", 20, 60);
        drawBitmapString("  tcdebug info          - Get app info (JSON)", 20, 80);
        drawBitmapString("  tcdebug screenshot    - Take screenshot", 20, 100);
        drawBitmapString("  spawn x y             - Spawn a ball", 20, 120);
        drawBitmapString("  clear                 - Clear all balls", 20, 140);

        // コマンドログを表示
        setColor(150);
        drawBitmapString("Recent commands:", 20, 180);
        int y = 200;
        for (const auto& cmd : commandLog_) {
            drawBitmapString("> " + cmd, 20, y);
            y += 16;
        }

        // ボールを描画
        for (const auto& ball : balls_) {
            setColor(ball.color);
            drawCircle(ball.x, ball.y, 20);
        }

        // ボール数を表示
        setColor(255);
        drawBitmapString("Balls: " + to_string(balls_.size()), 20, getWindowHeight() - 30);
    }

private:
    vector<Ball> balls_;
    deque<string> commandLog_;
    EventListener consoleListener_;
};
