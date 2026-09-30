#include "tcxOscMessage.h"
#include <sstream>

namespace tcx::osc {

using namespace osc_internal;

namespace {

// Move pos past the zero padding that aligns it to 4 bytes. Requires
// pos <= size, and the result stays <= size: padding missing at the very end
// is tolerated (as before), the next read then finds no data left.
size_t skipPadding(size_t pos, size_t size) {
    size_t pad = (4 - (pos & 3)) & 3;
    return pad > size - pos ? size : pos + pad;
}

}  // namespace

// =============================================================================
// toBytes - Serialize message to byte array
// =============================================================================
std::vector<uint8_t> OscMessage::toBytes() const {
    std::vector<uint8_t> result;

    // Address (null-terminated + padding)
    result.insert(result.end(), address_.begin(), address_.end());
    result.push_back(0);
    while (result.size() % 4 != 0) result.push_back(0);

    // Type tags (comma + tags + null-terminated + padding)
    result.push_back(',');
    result.insert(result.end(), typeTags_.begin(), typeTags_.end());
    result.push_back(0);
    while (result.size() % 4 != 0) result.push_back(0);

    // Argument data
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
        // 'T' and 'F' have no data
    }

    return result;
}

// =============================================================================
// fromBytes - Parse message from byte array (robust implementation)
// =============================================================================
OscMessage OscMessage::fromBytes(const uint8_t* data, size_t size, bool& ok) {
    ok = false;
    OscMessage msg;

    if (!data || size < 4) return msg;

    // pos <= size holds throughout, and each size check subtracts from size
    // instead of adding to pos, so none of them can wrap when size_t is 32
    // bits. An argument that runs past the end fails the parse.
    size_t pos = 0;

    // Read address
    if (data[pos] != '/') return msg;  // Address must start with '/'

    size_t addrEnd = findNull(data, size, pos);
    if (addrEnd == size_t(-1)) return msg;

    msg.address_ = std::string(reinterpret_cast<const char*>(data + pos), addrEnd - pos);
    pos = skipPadding(addrEnd + 1, size);

    if (pos >= size) {
        // Messages without arguments (no type tags) are allowed
        ok = true;
        return msg;
    }

    // Read type tags
    if (data[pos] != ',') {
        // Messages without type tags are allowed (old OSC spec)
        ok = true;
        return msg;
    }

    size_t typeTagStart = pos + 1;
    size_t typeTagEnd = findNull(data, size, typeTagStart);
    if (typeTagEnd == size_t(-1)) return msg;

    msg.typeTags_ = std::string(reinterpret_cast<const char*>(data + typeTagStart), typeTagEnd - typeTagStart);
    pos = skipPadding(typeTagEnd + 1, size);

    // Read arguments
    for (char type : msg.typeTags_) {
        if (type == 'i') {
            if (4 > size - pos) return msg;  // Insufficient size
            uint32_t be;
            std::memcpy(&be, data + pos, 4);
            int32_t value = static_cast<int32_t>(fromBigEndian(be));
            msg.args_.emplace_back(value);
            pos += 4;
        }
        else if (type == 'f') {
            if (4 > size - pos) return msg;
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
            pos = skipPadding(strEnd + 1, size);
        }
        else if (type == 'b') {
            if (4 > size - pos) return msg;
            uint32_t be;
            std::memcpy(&be, data + pos, 4);
            uint32_t blobSize = fromBigEndian(be);
            pos += 4;
            // A blob larger than the data left is truncated
            if (blobSize > size - pos) return msg;
            std::vector<uint8_t> blob(data + pos, data + pos + blobSize);
            msg.args_.emplace_back(std::move(blob));
            pos = skipPadding(pos + blobSize, size);
        }
        else if (type == 'T') {
            msg.args_.emplace_back(true);
        }
        else if (type == 'F') {
            msg.args_.emplace_back(false);
        }
        else {
            // Unknown type tags are skipped (for robustness)
            // But size is unknown, so stop here
            break;
        }
    }

    ok = true;
    return msg;
}

// =============================================================================
// toString - Debug string
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

}  // namespace tcx::osc
