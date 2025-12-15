#pragma once

#include "tcBaseApp.h"
#include <deque>
#include <string>

using namespace trussc;

// eventsExample - イベントシステムのデモ
// EventListenerのRAII動作とイベント購読を示す

class tcApp : public tc::App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // イベントリスナー（メンバとして保持）
    EventListener keyListener;
    EventListener mouseListener;
    EventListener scrollListener;

    // イベントログ
    std::deque<std::string> eventLog;
    static const size_t MAX_LOG_LINES = 20;

    // リスナーの有効状態
    bool keyListenerActive = true;
    bool mouseListenerActive = true;

    void addLog(const std::string& msg);
    void setupListeners();
};
