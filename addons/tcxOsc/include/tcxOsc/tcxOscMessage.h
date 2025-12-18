#pragma once

#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <cstring>

namespace trussc {

// =============================================================================
// エンディアン変換ユーティリティ
// =============================================================================
namespace osc_internal {

// 4バイト境界にアラインメント
inline size_t alignTo4(size_t pos) {
    return (pos + 3) & ~3;
}

// ビッグエンディアン変換（ネットワークバイトオーダー）
inline uint32_t toBigEndian(uint32_t value) {
    uint8_t* p = reinterpret_cast<uint8_t*>(&value);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline uint32_t fromBigEndian(uint32_t value) {
    return toBigEndian(value);  // 対称的
}

inline uint64_t toBigEndian64(uint64_t value) {
    uint8_t* p = reinterpret_cast<uint8_t*>(&value);
    return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) |
           (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) |
           (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) |
           (uint64_t(p[6]) << 8) | uint64_t(p[7]);
}

inline uint64_t fromBigEndian64(uint64_t value) {
    return toBigEndian64(value);
}

// float <-> uint32 のビット変換
inline uint32_t floatToUint32(float f) {
    uint32_t result;
    std::memcpy(&result, &f, sizeof(float));
    return result;
}

inline float uint32ToFloat(uint32_t u) {
    float result;
    std::memcpy(&result, &u, sizeof(float));
    return result;
}

// null終端を探す
inline size_t findNull(const uint8_t* data, size_t size, size_t start) {
    for (size_t i = start; i < size; ++i) {
        if (data[i] == 0) return i;
    }
    return size_t(-1);  // 見つからない
}

}  // namespace osc_internal

// =============================================================================
// OscMessage - OSC メッセージクラス
// =============================================================================
class OscMessage {
public:
    // 引数の型
    using ArgVariant = std::variant<int32_t, float, std::string, std::vector<uint8_t>, bool>;

    OscMessage() = default;
    explicit OscMessage(const std::string& address) : address_(address) {}

    // -------------------------------------------------------------------------
    // アドレス
    // -------------------------------------------------------------------------
    void setAddress(const std::string& address) { address_ = address; }
    std::string getAddress() const { return address_; }

    // -------------------------------------------------------------------------
    // 引数追加
    // -------------------------------------------------------------------------
    OscMessage& addInt(int32_t value) {
        typeTags_ += 'i';
        args_.emplace_back(value);
        return *this;
    }

    OscMessage& addFloat(float value) {
        typeTags_ += 'f';
        args_.emplace_back(value);
        return *this;
    }

    OscMessage& addString(const std::string& value) {
        typeTags_ += 's';
        args_.emplace_back(value);
        return *this;
    }

    OscMessage& addBlob(const void* data, size_t size) {
        typeTags_ += 'b';
        std::vector<uint8_t> blob(static_cast<const uint8_t*>(data),
                                   static_cast<const uint8_t*>(data) + size);
        args_.emplace_back(std::move(blob));
        return *this;
    }

    OscMessage& addBool(bool value) {
        typeTags_ += value ? 'T' : 'F';
        args_.emplace_back(value);
        return *this;
    }

    // -------------------------------------------------------------------------
    // 引数取得
    // -------------------------------------------------------------------------
    size_t getArgCount() const { return args_.size(); }
    std::string getTypeTags() const { return typeTags_; }

    char getArgType(size_t index) const {
        if (index >= typeTags_.size()) return '\0';
        return typeTags_[index];
    }

    int32_t getArgAsInt(size_t index) const {
        if (index >= args_.size()) return 0;
        if (auto* v = std::get_if<int32_t>(&args_[index])) return *v;
        if (auto* v = std::get_if<float>(&args_[index])) return static_cast<int32_t>(*v);
        return 0;
    }

    float getArgAsFloat(size_t index) const {
        if (index >= args_.size()) return 0.0f;
        if (auto* v = std::get_if<float>(&args_[index])) return *v;
        if (auto* v = std::get_if<int32_t>(&args_[index])) return static_cast<float>(*v);
        return 0.0f;
    }

    std::string getArgAsString(size_t index) const {
        if (index >= args_.size()) return "";
        if (auto* v = std::get_if<std::string>(&args_[index])) return *v;
        return "";
    }

    std::vector<uint8_t> getArgAsBlob(size_t index) const {
        if (index >= args_.size()) return {};
        if (auto* v = std::get_if<std::vector<uint8_t>>(&args_[index])) return *v;
        return {};
    }

    bool getArgAsBool(size_t index) const {
        if (index >= args_.size()) return false;
        if (auto* v = std::get_if<bool>(&args_[index])) return *v;
        return false;
    }

    // -------------------------------------------------------------------------
    // シリアライズ
    // -------------------------------------------------------------------------
    std::vector<uint8_t> toBytes() const;
    static OscMessage fromBytes(const uint8_t* data, size_t size, bool& ok);

    // -------------------------------------------------------------------------
    // デバッグ用文字列
    // -------------------------------------------------------------------------
    std::string toString() const;

    // -------------------------------------------------------------------------
    // クリア
    // -------------------------------------------------------------------------
    void clear() {
        address_.clear();
        typeTags_.clear();
        args_.clear();
    }

private:
    std::string address_;
    std::string typeTags_;
    std::vector<ArgVariant> args_;
};

}  // namespace trussc
