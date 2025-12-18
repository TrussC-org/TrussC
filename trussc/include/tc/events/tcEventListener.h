#pragma once

// =============================================================================
// tcEventListener - イベントリスナーのRAIIトークン
// =============================================================================

#include <functional>
#include <utility>

namespace trussc {

// EventListener - リスナー登録の RAII トークン
// スコープを外れると自動的にリスナーが解除される
class EventListener {
public:
    using DisconnectFunc = std::function<void()>;

    // デフォルトコンストラクタ（未接続状態）
    EventListener() = default;

    // ムーブコンストラクタ
    EventListener(EventListener&& other) noexcept
        : disconnector_(std::move(other.disconnector_))
        , connected_(other.connected_) {
        other.connected_ = false;
        other.disconnector_ = nullptr;
    }

    // ムーブ代入演算子
    EventListener& operator=(EventListener&& other) noexcept {
        if (this != &other) {
            disconnect();
            disconnector_ = std::move(other.disconnector_);
            connected_ = other.connected_;
            other.connected_ = false;
            other.disconnector_ = nullptr;
        }
        return *this;
    }

    // コピー禁止（所有権は一意）
    EventListener(const EventListener&) = delete;
    EventListener& operator=(const EventListener&) = delete;

    // デストラクタ - 自動で disconnect
    ~EventListener() {
        disconnect();
    }

    // 明示的に切断
    void disconnect() {
        if (connected_ && disconnector_) {
            disconnector_();
            connected_ = false;
            disconnector_ = nullptr;
        }
    }

    // 接続状態を確認
    bool isConnected() const {
        return connected_;
    }

    // 明示的にブール変換
    explicit operator bool() const {
        return connected_;
    }

private:
    // Event<T> からのみ生成可能
    template<typename T> friend class Event;

    // 内部コンストラクタ
    explicit EventListener(DisconnectFunc disconnector)
        : disconnector_(std::move(disconnector))
        , connected_(true) {}

    DisconnectFunc disconnector_;
    bool connected_ = false;
};

} // namespace trussc
