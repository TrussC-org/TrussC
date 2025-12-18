#pragma once

#include "tcxOscMessage.h"
#include <variant>

namespace trussc {

// =============================================================================
// OscBundle - OSC バンドルクラス
// バンドルは複数のメッセージや他のバンドルを含むことができる
// =============================================================================
class OscBundle {
public:
    // バンドルの要素（メッセージまたはバンドル）
    using Element = std::variant<OscMessage, OscBundle>;

    // 即時実行を表すタイムタグ
    static constexpr uint64_t TIMETAG_IMMEDIATELY = 1;

    OscBundle() : timetag_(TIMETAG_IMMEDIATELY) {}
    explicit OscBundle(uint64_t timetag) : timetag_(timetag) {}

    // -------------------------------------------------------------------------
    // タイムタグ（NTP形式）
    // -------------------------------------------------------------------------
    void setTimetag(uint64_t timetag) { timetag_ = timetag; }
    uint64_t getTimetag() const { return timetag_; }

    // -------------------------------------------------------------------------
    // 要素追加
    // -------------------------------------------------------------------------
    OscBundle& addMessage(const OscMessage& msg) {
        elements_.emplace_back(msg);
        return *this;
    }

    OscBundle& addBundle(const OscBundle& bundle) {
        elements_.emplace_back(bundle);
        return *this;
    }

    // -------------------------------------------------------------------------
    // 要素取得
    // -------------------------------------------------------------------------
    size_t getElementCount() const { return elements_.size(); }

    bool isBundle(size_t index) const {
        if (index >= elements_.size()) return false;
        return std::holds_alternative<OscBundle>(elements_[index]);
    }

    bool isMessage(size_t index) const {
        if (index >= elements_.size()) return false;
        return std::holds_alternative<OscMessage>(elements_[index]);
    }

    OscMessage getMessageAt(size_t index) const {
        if (index >= elements_.size()) return OscMessage();
        if (auto* msg = std::get_if<OscMessage>(&elements_[index])) {
            return *msg;
        }
        return OscMessage();
    }

    OscBundle getBundleAt(size_t index) const {
        if (index >= elements_.size()) return OscBundle();
        if (auto* bundle = std::get_if<OscBundle>(&elements_[index])) {
            return *bundle;
        }
        return OscBundle();
    }

    // -------------------------------------------------------------------------
    // シリアライズ
    // -------------------------------------------------------------------------
    std::vector<uint8_t> toBytes() const;
    static OscBundle fromBytes(const uint8_t* data, size_t size, bool& ok);

    // -------------------------------------------------------------------------
    // バンドル判定（データの先頭が "#bundle" かどうか）
    // -------------------------------------------------------------------------
    static bool isBundle(const uint8_t* data, size_t size) {
        if (size < 8) return false;
        return data[0] == '#' && data[1] == 'b' && data[2] == 'u' &&
               data[3] == 'n' && data[4] == 'd' && data[5] == 'l' &&
               data[6] == 'e' && data[7] == '\0';
    }

    // -------------------------------------------------------------------------
    // クリア
    // -------------------------------------------------------------------------
    void clear() {
        timetag_ = TIMETAG_IMMEDIATELY;
        elements_.clear();
    }

private:
    uint64_t timetag_;
    std::vector<Element> elements_;
};

}  // namespace trussc
