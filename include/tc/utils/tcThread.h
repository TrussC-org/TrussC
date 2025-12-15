#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace trussc {

// ---------------------------------------------------------------------------
// Thread - スレッド基底クラス（ofThread互換）
// ---------------------------------------------------------------------------
//
// 使い方:
// 1. Thread を継承したクラスを作成
// 2. threadedFunction() をオーバーライドして実装
// 3. startThread() でスレッド開始
// 4. stopThread() でスレッド停止シグナル
// 5. waitForThread() でスレッド終了を待機
//
// 例:
//   class MyThread : public tc::Thread {
//   protected:
//       void threadedFunction() override {
//           while (isThreadRunning()) {
//               // 処理
//           }
//       }
//   };
//
// Mutex の使用:
//   ドキュメントに記載の通り、独自ラッパーは提供しない。
//   std::mutex と std::lock_guard の使用を推奨。
//
// ---------------------------------------------------------------------------

class Thread {
public:
    Thread() : threadRunning_(false) {}

    virtual ~Thread() {
        // スレッドが実行中なら停止して待機
        if (isThreadRunning()) {
            stopThread();
            waitForThread(false);
        }
    }

    // コピー禁止
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // ムーブは許可（ただしスレッド実行中は不可）
    Thread(Thread&& other) noexcept : threadRunning_(false) {
        // 移動元が実行中でないことを確認
        if (other.isThreadRunning()) {
            // 実行中のスレッドはムーブできない
            return;
        }
    }

    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (isThreadRunning()) {
                stopThread();
                waitForThread(false);
            }
        }
        return *this;
    }

    // ---------------------------------------------------------------------------
    // スレッド制御
    // ---------------------------------------------------------------------------

    // スレッドを開始
    void startThread() {
        if (isThreadRunning()) return;

        // 前のスレッドが残っていたら join する
        if (thread_.joinable()) {
            thread_.join();
        }

        threadRunning_ = true;
        thread_ = std::thread([this]() {
            threadedFunction();
            threadRunning_ = false;
        });
    }

    // スレッドに停止シグナルを送る
    // threadedFunction内で isThreadRunning() がfalseを返すようになる
    void stopThread() {
        threadRunning_ = false;
    }

    // スレッドの終了を待機
    // callStopThread: trueの場合、stopThread()を先に呼ぶ
    void waitForThread(bool callStopThread = true) {
        if (callStopThread) {
            stopThread();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // スレッドが実行中かどうか
    bool isThreadRunning() const {
        return threadRunning_;
    }

    // スレッドIDを取得
    std::thread::id getThreadId() const {
        return thread_.get_id();
    }

    // ---------------------------------------------------------------------------
    // ユーティリティ
    // ---------------------------------------------------------------------------

    // 現在のスレッドを一時停止
    static void sleep(unsigned long milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // 他のスレッドに実行機会を譲る
    static void yield() {
        std::this_thread::yield();
    }

    // 現在のスレッドがメインスレッドかどうか
    // 注: この関数を使うには、最初に一度メインスレッドから呼んでIDを記録する必要がある
    static bool isCurrentThreadTheMainThread() {
        return std::this_thread::get_id() == getMainThreadId();
    }

    // メインスレッドIDを取得/設定
    // 最初の呼び出しで現在のスレッドIDを記録
    static std::thread::id getMainThreadId() {
        static std::thread::id mainThreadId = std::this_thread::get_id();
        return mainThreadId;
    }

protected:
    // ---------------------------------------------------------------------------
    // サブクラスで実装する関数
    // ---------------------------------------------------------------------------

    // スレッドで実行される処理
    // サブクラスでオーバーライドして実装する
    // while (isThreadRunning()) { ... } のループを使用することを推奨
    virtual void threadedFunction() = 0;

    // ---------------------------------------------------------------------------
    // ミューテックス（サブクラスで使用可能）
    // ---------------------------------------------------------------------------

    // スレッド間でデータを共有する場合に使用
    std::mutex mutex;

private:
    std::thread thread_;
    std::atomic<bool> threadRunning_;
};

// ---------------------------------------------------------------------------
// ヘルパー関数
// ---------------------------------------------------------------------------

// メインスレッドIDを取得（エイリアス）
inline std::thread::id getMainThreadId() {
    return Thread::getMainThreadId();
}

// 現在のスレッドがメインスレッドかどうか
inline bool isMainThread() {
    return Thread::isCurrentThreadTheMainThread();
}

} // namespace trussc
