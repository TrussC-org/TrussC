#pragma once

#include "tcBaseApp.h"
#include <set>
#include <deque>
#include <string>

using namespace trussc;

// keyboardExample - キーボード入力のデモ
// キーの押下状態、修飾キー、キーコードの可視化

class tcApp : public tc::App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;
    void keyReleased(int key) override;

private:
    // 現在押されているキー
    std::set<int> pressedKeys;

    // 最後に押されたキー
    int lastKey = 0;

    // キー入力履歴
    std::deque<std::string> keyHistory;
    static const size_t MAX_HISTORY = 15;

    // ボックスの位置（矢印キーで移動）
    float boxX = 400;
    float boxY = 300;
    float boxSpeed = 5.0f;

    void addHistory(const std::string& msg);
    std::string keyToString(int key);
};
