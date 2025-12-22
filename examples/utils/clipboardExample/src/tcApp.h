#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>
#include <deque>

using namespace std;

// =============================================================================
// clipboardExample - クリップボードAPIのサンプル
// =============================================================================
// stdinからコマンドを受け取ってクリップボードを操作する。
// clipboardSizeを100バイトに設定し、オーバーフロー警告をテストできる。
//
// 使い方（ターミナルから実行）:
//   ./clipboardExample
//   >>> set Hello World        # "Hello World"をクリップボードにセット
//   >>> get                    # クリップボードの内容を取得
//   >>> overflow               # 100バイト以上のテキストをセット（警告が出る）
//
// パイプで送る場合:
//   echo "set test" | ./clipboardExample
//
// =============================================================================

class tcApp : public App {
public:
    void setup() override {
        // コンソールイベントをリッスン
        events().console.listen(consoleListener_, [this](ConsoleEventArgs& e) {
            // ログに追加（最新10件まで）
            commandLog_.push_back(e.raw);
            if (commandLog_.size() > 10) {
                commandLog_.pop_front();
            }

            if (e.args.empty()) return;

            if (e.args[0] == "set" && e.args.size() >= 2) {
                // set <text> - クリップボードにセット
                // スペースで区切られた残りをすべて結合
                string text;
                for (size_t i = 1; i < e.args.size(); i++) {
                    if (i > 1) text += " ";
                    text += e.args[i];
                }
                setClipboardString(text);
                lastResult_ = "Set: \"" + text + "\" (" + to_string(text.size()) + " bytes)";
                cout << "{\"status\":\"ok\",\"command\":\"set\",\"size\":" << text.size() << "}" << endl;
            }
            else if (e.args[0] == "get") {
                // get - クリップボードから取得
                string text = getClipboardString();
                lastResult_ = "Get: \"" + text + "\" (" + to_string(text.size()) + " bytes)";
                cout << "{\"status\":\"ok\",\"command\":\"get\",\"text\":\"" << text << "\"}" << endl;
            }
            else if (e.args[0] == "overflow") {
                // overflow - 100バイト以上のテキストをセット（警告テスト）
                string longText(150, 'X');  // 150バイトのテキスト
                setClipboardString(longText);
                lastResult_ = "Set overflow test: 150 bytes (check console for warning)";
                cout << "{\"status\":\"ok\",\"command\":\"overflow\",\"size\":150}" << endl;
            }
        });

        cout << "clipboardExample started. Buffer size: 100 bytes" << endl;
        cout << "Commands: set <text>, get, overflow" << endl;
    }

    void draw() override {
        clear(30);

        // 使い方表示
        setColor(200);
        drawBitmapString("Clipboard Example - stdin commands", 20, 30);
        drawBitmapString("Buffer size: 100 bytes (for overflow testing)", 20, 50);
        drawBitmapString("", 20, 70);
        drawBitmapString("Commands:", 20, 90);
        drawBitmapString("  set <text>   - Set text to clipboard", 20, 110);
        drawBitmapString("  get          - Get text from clipboard", 20, 130);
        drawBitmapString("  overflow     - Set 150 bytes (triggers warning)", 20, 150);

        // 最後の結果
        setColor(100, 255, 100);
        drawBitmapString("Result: " + lastResult_, 20, 190);

        // コマンドログ
        setColor(150);
        drawBitmapString("Recent commands:", 20, 230);
        int y = 250;
        for (const auto& cmd : commandLog_) {
            drawBitmapString("> " + cmd, 20, y);
            y += 16;
        }
    }

private:
    deque<string> commandLog_;
    string lastResult_;
    EventListener consoleListener_;
};
