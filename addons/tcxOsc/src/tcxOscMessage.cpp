#include "tcxOsc/tcxOscMessage.h"
#include <sstream>

namespace trussc {

using namespace osc_internal;

// =============================================================================
// toBytes - メッセージをバイト列にシリアライズ
// =============================================================================
std::vector<uint8_t> OscMessage::toBytes() const {
    std::vector<uint8_t> result;

    // アドレス（null終端 + パディング）
    result.insert(result.end(), address_.begin(), address_.end());
    result.push_back(0);
    while (result.size() % 4 != 0) result.push_back(0);

    // 型タグ（カンマ + タグ + null終端 + パディング）
    result.push_back(',');
    result.insert(result.end(), typeTags_.begin(), typeTags_.end());
    result.push_back(0);
    while (result.size() % 4 != 0) result.push_back(0);

    // 引数データ
    for (size_t i = 0; i < args_.size(); ++i) {
        char type = typeTags_[i];

        if (type == 'i') {
            int32_t value = std::get<int32_t>(args_[i]);
            uint32_t be = toBigEndian(static_cast<uint32_t>(value));
            auto* p = reinterpret_cast<uint8_t*>(&be);
            result.insert(result.end(), p, p + 4);
        }
        else if (type == 'f') {
            float value = std::get<float>(args_[i]);
            uint32_t be = toBigEndian(floatToUint32(value));
            auto* p = reinterpret_cast<uint8_t*>(&be);
            result.insert(result.end(), p, p + 4);
        }
        else if (type == 's') {
            const std::string& str = std::get<std::string>(args_[i]);
            result.insert(result.end(), str.begin(), str.end());
            result.push_back(0);
            while (result.size() % 4 != 0) result.push_back(0);
        }
        else if (type == 'b') {
            const auto& blob = std::get<std::vector<uint8_t>>(args_[i]);
            uint32_t size = static_cast<uint32_t>(blob.size());
            uint32_t be = toBigEndian(size);
            auto* p = reinterpret_cast<uint8_t*>(&be);
            result.insert(result.end(), p, p + 4);
            result.insert(result.end(), blob.begin(), blob.end());
            while (result.size() % 4 != 0) result.push_back(0);
        }
        // 'T' と 'F' はデータなし
    }

    return result;
}

// =============================================================================
// fromBytes - バイト列からメッセージをパース（ロバスト実装）
// =============================================================================
OscMessage OscMessage::fromBytes(const uint8_t* data, size_t size, bool& ok) {
    ok = false;
    OscMessage msg;

    if (!data || size < 4) return msg;

    size_t pos = 0;

    // アドレス読み取り
    if (data[pos] != '/') return msg;  // アドレスは '/' で始まる

    size_t addrEnd = findNull(data, size, pos);
    if (addrEnd == size_t(-1)) return msg;

    msg.address_ = std::string(reinterpret_cast<const char*>(data + pos), addrEnd - pos);
    pos = alignTo4(addrEnd + 1);

    if (pos >= size) {
        // 引数なしメッセージ（型タグなし）は許容
        ok = true;
        return msg;
    }

    // 型タグ読み取り
    if (data[pos] != ',') {
        // 型タグがないメッセージも許容（古い OSC 仕様）
        ok = true;
        return msg;
    }

    size_t typeTagStart = pos + 1;
    size_t typeTagEnd = findNull(data, size, typeTagStart);
    if (typeTagEnd == size_t(-1)) return msg;

    msg.typeTags_ = std::string(reinterpret_cast<const char*>(data + typeTagStart), typeTagEnd - typeTagStart);
    pos = alignTo4(typeTagEnd + 1);

    // 引数読み取り
    for (char type : msg.typeTags_) {
        if (type == 'i') {
            if (pos + 4 > size) return msg;  // サイズ不足
            uint32_t be;
            std::memcpy(&be, data + pos, 4);
            int32_t value = static_cast<int32_t>(fromBigEndian(be));
            msg.args_.emplace_back(value);
            pos += 4;
        }
        else if (type == 'f') {
            if (pos + 4 > size) return msg;
            uint32_t be;
            std::memcpy(&be, data + pos, 4);
            float value = uint32ToFloat(fromBigEndian(be));
            msg.args_.emplace_back(value);
            pos += 4;
        }
        else if (type == 's') {
            size_t strEnd = findNull(data, size, pos);
            if (strEnd == size_t(-1)) return msg;
            std::string str(reinterpret_cast<const char*>(data + pos), strEnd - pos);
            msg.args_.emplace_back(std::move(str));
            pos = alignTo4(strEnd + 1);
        }
        else if (type == 'b') {
            if (pos + 4 > size) return msg;
            uint32_t be;
            std::memcpy(&be, data + pos, 4);
            uint32_t blobSize = fromBigEndian(be);
            pos += 4;
            if (pos + blobSize > size) return msg;
            std::vector<uint8_t> blob(data + pos, data + pos + blobSize);
            msg.args_.emplace_back(std::move(blob));
            pos = alignTo4(pos + blobSize);
        }
        else if (type == 'T') {
            msg.args_.emplace_back(true);
        }
        else if (type == 'F') {
            msg.args_.emplace_back(false);
        }
        else {
            // 未知の型タグはスキップ（ロバスト性のため）
            // ただしサイズが不明なのでここで終了
            break;
        }
    }

    ok = true;
    return msg;
}

// =============================================================================
// toString - デバッグ用文字列
// =============================================================================
std::string OscMessage::toString() const {
    std::ostringstream oss;
    oss << address_;

    for (size_t i = 0; i < args_.size(); ++i) {
        oss << " ";
        char type = (i < typeTags_.size()) ? typeTags_[i] : '?';

        if (type == 'i') {
            oss << "i:" << std::get<int32_t>(args_[i]);
        }
        else if (type == 'f') {
            oss << "f:" << std::get<float>(args_[i]);
        }
        else if (type == 's') {
            oss << "s:\"" << std::get<std::string>(args_[i]) << "\"";
        }
        else if (type == 'b') {
            oss << "b:[" << std::get<std::vector<uint8_t>>(args_[i]).size() << " bytes]";
        }
        else if (type == 'T') {
            oss << "T";
        }
        else if (type == 'F') {
            oss << "F";
        }
    }

    return oss.str();
}

}  // namespace trussc
