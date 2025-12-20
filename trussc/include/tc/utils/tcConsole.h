#pragma once

// =============================================================================
// tcConsole - stdin からのコマンド入力機能
// =============================================================================
//
// AI アシスタントなど外部プロセスからのコマンドを受け取るためのモジュール。
// stdin で受け取った行をパースして、ConsoleEventArgs としてイベント配信する。
//
// 使い方:
//   // 受信側（tcApp::setup() 内など）
//   tcEvents().console.listen([](const ConsoleEventArgs& e) {
//       if (e.args[0] == "spawn") {
//           spawnEnemy(stoi(e.args[1]), stoi(e.args[2]));
//       }
//   });
//
//   // 送信側（外部プロセス）
//   echo "spawn 100 200" | ./myapp
//
// デフォルトで有効。無効にしたい場合は setup() 内で console::stop() を呼ぶ。
//
// =============================================================================

#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <memory>
#include "tcThreadChannel.h"
#include "../events/tcEventArgs.h"
#include "../events/tcCoreEvents.h"

namespace trussc {
namespace console {

// ---------------------------------------------------------------------------
// 内部状態
// ---------------------------------------------------------------------------
namespace detail {

inline ThreadChannel<ConsoleEventArgs>& getChannel() {
    static ThreadChannel<ConsoleEventArgs> channel;
    return channel;
}

inline std::atomic<bool>& isRunning() {
    static std::atomic<bool> running{false};
    return running;
}

inline std::unique_ptr<std::thread>& getThread() {
    static std::unique_ptr<std::thread> t;
    return t;
}

// 行を空白でパースして ConsoleEventArgs を作成
inline ConsoleEventArgs parseLine(const std::string& line) {
    ConsoleEventArgs args;
    args.raw = line;

    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        args.args.push_back(token);
    }
    return args;
}

// stdin 読み取りスレッド
inline void readThread() {
    std::string line;
    while (isRunning().load() && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto args = parseLine(line);
        getChannel().send(std::move(args));
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// 公開API
// ---------------------------------------------------------------------------

/// コンソール入力スレッドを開始
/// TrussC.h の runApp() 内で自動的に呼ばれる
inline void start() {
    if (detail::isRunning().load()) {
        return; // すでに起動中
    }

    detail::isRunning().store(true);
    detail::getThread() = std::make_unique<std::thread>(detail::readThread);
}

/// コンソール入力スレッドを停止
/// 無効にしたい場合は setup() 内で呼ぶ
inline void stop() {
    if (!detail::isRunning().load()) {
        return; // すでに停止中
    }

    detail::isRunning().store(false);
    detail::getChannel().close();

    // スレッドが getline で待機している場合、
    // stdin を閉じない限りブロックし続ける可能性があるので detach する
    if (detail::getThread() && detail::getThread()->joinable()) {
        detail::getThread()->detach();
    }
    detail::getThread().reset();
}

/// キューに溜まったコマンドを処理
/// TrussC.h の _frame_cb() 内で毎フレーム呼ばれる
/// コマンドがあれば events().console にイベントを発火する
inline void processQueue() {
    if (!detail::isRunning().load()) {
        return;
    }

    ConsoleEventArgs args;
    while (detail::getChannel().tryReceive(args)) {
        events().console.notify(args);
    }
}

/// コンソールが有効かどうか
inline bool isEnabled() {
    return detail::isRunning().load();
}

} // namespace console
} // namespace trussc
