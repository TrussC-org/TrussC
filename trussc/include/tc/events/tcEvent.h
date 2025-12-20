#pragma once

// =============================================================================
// tcEvent - 汎用イベントクラス
// =============================================================================

#include <functional>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include "tcEventListener.h"

namespace trussc {

// ---------------------------------------------------------------------------
// イベント優先度
// ---------------------------------------------------------------------------
enum class EventPriority : int {
    BeforeApp = 0,    // アプリより先に処理
    App = 100,        // 通常のアプリ処理（デフォルト）
    AfterApp = 200    // アプリの後に処理
};

// ---------------------------------------------------------------------------
// Event<T> - 引数ありイベント
// ---------------------------------------------------------------------------
template<typename T>
class Event {
public:
    // コールバック型（引数は参照渡し - 書き換え可能）
    using Callback = std::function<void(T&)>;

    Event() = default;
    ~Event() = default;

    // コピー・ムーブ禁止（イベントは固定位置）
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // ラムダ式でリスナーを登録（EventListenerを参照で受け取る）
    void listen(EventListener& listener, Callback callback,
                EventPriority priority = EventPriority::App) {
        uint64_t id;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            id = nextId_++;
            entries_.push_back({id, static_cast<int>(priority), std::move(callback)});
            sortEntries();
        }
        // ロック外でEventListenerを設定（既存接続の切断でremoveListener()が呼ばれるため）
        listener = EventListener([this, id]() {
            this->removeListener(id);
        });
    }

    // メンバ関数でリスナーを登録
    template<typename Obj>
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(T&),
                EventPriority priority = EventPriority::App) {
        listen(listener, [obj, method](T& arg) {
            (obj->*method)(arg);
        }, priority);
    }

    // イベントを発火
    void notify(T& arg) {
        std::vector<Entry> entriesCopy;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            entriesCopy = entries_;
        }
        // ロック外で実行（デッドロック防止）
        for (auto& entry : entriesCopy) {
            if (entry.callback) {
                entry.callback(arg);
            }
        }
    }

    // リスナー数を取得
    size_t listenerCount() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return entries_.size();
    }

    // すべてのリスナーを削除
    void clear() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Callback callback;
    };

    void removeListener(uint64_t id) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [id](const Entry& e) { return e.id == id; }),
            entries_.end()
        );
    }

    void sortEntries() {
        std::stable_sort(entries_.begin(), entries_.end(),
            [](const Entry& a, const Entry& b) {
                return a.priority < b.priority;
            });
    }

    mutable std::recursive_mutex mutex_;
    std::vector<Entry> entries_;
    uint64_t nextId_ = 0;
};

// ---------------------------------------------------------------------------
// Event<void> - 引数なしイベントの特殊化
// ---------------------------------------------------------------------------
template<>
class Event<void> {
public:
    // コールバック型（引数なし）
    using Callback = std::function<void()>;

    Event() = default;
    ~Event() = default;

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // ラムダ式でリスナーを登録（EventListenerを参照で受け取る）
    void listen(EventListener& listener, Callback callback,
                EventPriority priority = EventPriority::App) {
        uint64_t id;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            id = nextId_++;
            entries_.push_back({id, static_cast<int>(priority), std::move(callback)});
            sortEntries();
        }
        // ロック外でEventListenerを設定（既存接続の切断でremoveListener()が呼ばれるため）
        listener = EventListener([this, id]() {
            this->removeListener(id);
        });
    }

    // メンバ関数でリスナーを登録
    template<typename Obj>
    void listen(EventListener& listener, Obj* obj, void (Obj::*method)(),
                EventPriority priority = EventPriority::App) {
        listen(listener, [obj, method]() {
            (obj->*method)();
        }, priority);
    }

    // イベントを発火
    void notify() {
        std::vector<Entry> entriesCopy;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            entriesCopy = entries_;
        }
        for (auto& entry : entriesCopy) {
            if (entry.callback) {
                entry.callback();
            }
        }
    }

    size_t listenerCount() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return entries_.size();
    }

    void clear() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_.clear();
    }

private:
    struct Entry {
        uint64_t id;
        int priority;
        Callback callback;
    };

    void removeListener(uint64_t id) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [id](const Entry& e) { return e.id == id; }),
            entries_.end()
        );
    }

    void sortEntries() {
        std::stable_sort(entries_.begin(), entries_.end(),
            [](const Entry& a, const Entry& b) {
                return a.priority < b.priority;
            });
    }

    mutable std::recursive_mutex mutex_;
    std::vector<Entry> entries_;
    uint64_t nextId_ = 0;
};

} // namespace trussc
