#pragma once

#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>

namespace trussc {

// ---------------------------------------------------------------------------
// ThreadChannel - スレッド間通信用のスレッドセーフキュー（ofThreadChannel互換）
// ---------------------------------------------------------------------------
//
// Producer-Consumer パターンの実装。
// 一方向通信用。双方向通信には2つのチャンネルを使用する。
//
// 使い方:
//   // 送信側（ワーカースレッド）
//   channel.send(data);
//
//   // 受信側（メインスレッド）
//   DataType received;
//   if (channel.receive(received)) {
//       // 受信成功
//   }
//
// データはFIFO（先入れ先出し）順で送受信される。
//
// ---------------------------------------------------------------------------

template<typename T>
class ThreadChannel {
public:
    ThreadChannel() : closed_(false) {}

    // コピー禁止
    ThreadChannel(const ThreadChannel&) = delete;
    ThreadChannel& operator=(const ThreadChannel&) = delete;

    // ---------------------------------------------------------------------------
    // 送信
    // ---------------------------------------------------------------------------

    // 値を送信（コピー）
    // チャンネルが閉じている場合は false を返す
    bool send(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        queue_.push(value);
        condition_.notify_one();
        return true;
    }

    // 値を送信（ムーブ）
    // チャンネルが閉じている場合は false を返す
    // 注意: 失敗しても value は無効化される
    bool send(T&& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        queue_.push(std::move(value));
        condition_.notify_one();
        return true;
    }

    // ---------------------------------------------------------------------------
    // 受信
    // ---------------------------------------------------------------------------

    // 値を受信（ブロッキング）
    // データが届くまで待機する
    // チャンネルが閉じられた場合は false を返す
    bool receive(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        while (queue_.empty() && !closed_) {
            condition_.wait(lock);
        }
        if (!closed_ && !queue_.empty()) {
            std::swap(value, queue_.front());
            queue_.pop();
            return true;
        }
        return false;
    }

    // 値を受信（ノンブロッキング）
    // データがなければ即座に false を返す
    bool tryReceive(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_ || queue_.empty()) {
            return false;
        }
        std::swap(value, queue_.front());
        queue_.pop();
        return true;
    }

    // 値を受信（タイムアウト付き）
    // 指定時間だけ待機し、データがなければ false を返す
    bool tryReceive(T& value, int64_t timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        if (queue_.empty()) {
            condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs));
            if (queue_.empty()) {
                return false;
            }
        }
        if (!closed_ && !queue_.empty()) {
            std::swap(value, queue_.front());
            queue_.pop();
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // 制御
    // ---------------------------------------------------------------------------

    // チャンネルを閉じる
    // 待機中のすべてのスレッドを起こす
    // 閉じた後は send/receive は false を返す
    void close() {
        std::unique_lock<std::mutex> lock(mutex_);
        closed_ = true;
        condition_.notify_all();
    }

    // キューをクリア
    void clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_ = {};
    }

    // ---------------------------------------------------------------------------
    // 状態取得
    // ---------------------------------------------------------------------------

    // キューが空かどうか（近似値）
    bool empty() const {
        return queue_.empty();
    }

    // キューのサイズ（近似値）
    size_t size() const {
        return queue_.size();
    }

    // チャンネルが閉じているかどうか
    bool isClosed() const {
        return closed_;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool closed_;
};

} // namespace trussc
