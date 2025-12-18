#include "tcxOsc/tcxOscBundle.h"

namespace trussc {

using namespace osc_internal;

// =============================================================================
// toBytes - バンドルをバイト列にシリアライズ
// =============================================================================
std::vector<uint8_t> OscBundle::toBytes() const {
    std::vector<uint8_t> result;

    // "#bundle\0"
    const char* bundleId = "#bundle";
    result.insert(result.end(), bundleId, bundleId + 8);

    // タイムタグ（8バイト、ビッグエンディアン）
    uint64_t be = toBigEndian64(timetag_);
    auto* p = reinterpret_cast<uint8_t*>(&be);
    result.insert(result.end(), p, p + 8);

    // 各要素
    for (const auto& element : elements_) {
        std::vector<uint8_t> elementBytes;

        if (auto* msg = std::get_if<OscMessage>(&element)) {
            elementBytes = msg->toBytes();
        }
        else if (auto* bundle = std::get_if<OscBundle>(&element)) {
            elementBytes = bundle->toBytes();
        }

        // サイズ（4バイト、ビッグエンディアン）
        uint32_t size = static_cast<uint32_t>(elementBytes.size());
        uint32_t sizeBe = toBigEndian(size);
        auto* sp = reinterpret_cast<uint8_t*>(&sizeBe);
        result.insert(result.end(), sp, sp + 4);

        // 要素データ
        result.insert(result.end(), elementBytes.begin(), elementBytes.end());
    }

    return result;
}

// =============================================================================
// fromBytes - バイト列からバンドルをパース（ロバスト実装）
// =============================================================================
OscBundle OscBundle::fromBytes(const uint8_t* data, size_t size, bool& ok) {
    ok = false;
    OscBundle bundle;

    // 最小サイズチェック: "#bundle\0" (8) + timetag (8) = 16
    if (!data || size < 16) return bundle;

    // バンドルID確認
    if (!isBundle(data, size)) return bundle;

    size_t pos = 8;

    // タイムタグ読み取り
    uint64_t be;
    std::memcpy(&be, data + pos, 8);
    bundle.timetag_ = fromBigEndian64(be);
    pos += 8;

    // 要素読み取り
    while (pos + 4 <= size) {
        // 要素サイズ
        uint32_t sizeBe;
        std::memcpy(&sizeBe, data + pos, 4);
        uint32_t elementSize = fromBigEndian(sizeBe);
        pos += 4;

        if (pos + elementSize > size) {
            // サイズ不正（残りデータが足りない）
            break;
        }

        const uint8_t* elementData = data + pos;

        // バンドルかメッセージか判定
        if (isBundle(elementData, elementSize)) {
            bool elementOk = false;
            OscBundle childBundle = fromBytes(elementData, elementSize, elementOk);
            if (elementOk) {
                bundle.elements_.emplace_back(std::move(childBundle));
            }
        }
        else {
            bool elementOk = false;
            OscMessage msg = OscMessage::fromBytes(elementData, elementSize, elementOk);
            if (elementOk) {
                bundle.elements_.emplace_back(std::move(msg));
            }
        }

        pos += elementSize;
    }

    ok = true;
    return bundle;
}

}  // namespace trussc
