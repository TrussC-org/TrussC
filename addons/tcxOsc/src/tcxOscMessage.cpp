#include "tcxOscMessage.h"
#include <sstream>

namespace tcx::osc {

using namespace osc_internal;

namespace {

// Move pos past the zero padding that aligns it to 4 bytes. Requires
// pos <= size, and the result stays <= size. When the data ends before the
// padding does, the result is size and paddingMissing is set; a read after
// that then finds no data left and fails.
size_t skipPadding(size_t pos, size_t size, bool& paddingMissing) {
    size_t pad = (4 - (pos & 3)) & 3;
    if (pad > size - pos) {
        paddingMissing = true;
        return size;
    }
    return pos + pad;
}

void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value >> 24));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}

void appendBe64(std::vector<uint8_t>& out, uint64_t value) {
    appendBe32(out, uint32_t(value >> 32));
    appendBe32(out, uint32_t(value));
}

uint32_t readBe32(const uint8_t* p) {
    uint32_t be;
    std::memcpy(&be, p, 4);
    return fromBigEndian(be);
}

uint64_t readBe64(const uint8_t* p) {
    uint64_t be;
    std::memcpy(&be, p, 8);
    return fromBigEndian64(be);
}

void appendPaddedString(std::vector<uint8_t>& out, const std::string& str) {
    out.insert(out.end(), str.begin(), str.end());
    out.push_back(0);
    while (out.size() % 4 != 0) out.push_back(0);
}

}  // namespace

// =============================================================================
// toBytes - Serialize message to byte array
// =============================================================================
std::vector<uint8_t> OscMessage::toBytes() const {
    std::vector<uint8_t> result;

    // Address (null-terminated + padding)
    appendPaddedString(result, address_);

    // Type tags (comma + tags + null-terminated + padding)
    appendPaddedString(result, "," + typeTags_);

    // Argument data, written by tag. T F N I [ ] have no data and no
    // stored argument.
    for (const StoredArg& stored : args_) {
        const ArgVariant& arg = stored.value;
        switch (typeTags_[stored.index]) {
            case 'i':
                appendBe32(result, static_cast<uint32_t>(std::get<int32_t>(arg)));
                break;
            case 'f':
                appendBe32(result, floatToUint32(std::get<float>(arg)));
                break;
            case 's':
            case 'S':
                appendPaddedString(result, std::get<std::string>(arg));
                break;
            case 'b': {
                const auto& blob = std::get<std::vector<uint8_t>>(arg);
                appendBe32(result, static_cast<uint32_t>(blob.size()));
                result.insert(result.end(), blob.begin(), blob.end());
                while (result.size() % 4 != 0) result.push_back(0);
                break;
            }
            case 'h':
                appendBe64(result, static_cast<uint64_t>(std::get<int64_t>(arg)));
                break;
            case 'd':
                appendBe64(result, doubleToUint64(std::get<double>(arg)));
                break;
            case 't':
                appendBe64(result, std::get<OscTimetag>(arg).value);
                break;
            case 'c':
                appendBe32(result, static_cast<uint8_t>(std::get<char>(arg)));
                break;
            case 'r': {
                const OscRgba& c = std::get<OscRgba>(arg);
                result.insert(result.end(), { c.r, c.g, c.b, c.a });
                break;
            }
            case 'm': {
                const OscMidi& m = std::get<OscMidi>(arg);
                result.insert(result.end(), { m.port, m.status, m.data1, m.data2 });
                break;
            }
            default:
                break;
        }
    }

    return result;
}

// =============================================================================
// fromBytes - Parse message from byte array (robust implementation)
// =============================================================================
OscMessage OscMessage::fromBytes(const uint8_t* data, size_t size, bool& ok) {
    bool paddingMissing = false;
    return fromBytes(data, size, ok, paddingMissing);
}

OscMessage OscMessage::fromBytes(const uint8_t* data, size_t size, bool& ok, bool& paddingMissing) {
    ok = false;
    paddingMissing = false;
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
    pos = skipPadding(addrEnd + 1, size, paddingMissing);

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
    pos = skipPadding(typeTagEnd + 1, size, paddingMissing);

    // Read arguments: one stored argument per tag with data. Every tag must
    // be an OSC 1.0 tag, and every argument's data must be there in full.
    int arrayDepth = 0;
    for (size_t index = 0; index < msg.typeTags_.size(); ++index) {
        switch (msg.typeTags_[index]) {
            case 'i':
                if (4 > size - pos) return msg;
                msg.storeArg<int32_t>(index, static_cast<int32_t>(readBe32(data + pos)));
                pos += 4;
                break;
            case 'f':
                if (4 > size - pos) return msg;
                msg.storeArg<float>(index, uint32ToFloat(readBe32(data + pos)));
                pos += 4;
                break;
            case 'c':
                if (4 > size - pos) return msg;
                // ASCII character in the low byte of 32 bits
                msg.storeArg<char>(index, static_cast<char>(readBe32(data + pos) & 0xFF));
                pos += 4;
                break;
            case 'r':
                if (4 > size - pos) return msg;
                msg.storeArg<OscRgba>(index, OscRgba{ data[pos], data[pos + 1], data[pos + 2], data[pos + 3] });
                pos += 4;
                break;
            case 'm':
                if (4 > size - pos) return msg;
                msg.storeArg<OscMidi>(index, OscMidi{ data[pos], data[pos + 1], data[pos + 2], data[pos + 3] });
                pos += 4;
                break;
            case 'h':
                if (8 > size - pos) return msg;
                msg.storeArg<int64_t>(index, static_cast<int64_t>(readBe64(data + pos)));
                pos += 8;
                break;
            case 'd':
                if (8 > size - pos) return msg;
                msg.storeArg<double>(index, uint64ToDouble(readBe64(data + pos)));
                pos += 8;
                break;
            case 't':
                if (8 > size - pos) return msg;
                msg.storeArg<OscTimetag>(index, OscTimetag{ readBe64(data + pos) });
                pos += 8;
                break;
            case 's':
            case 'S': {
                size_t strEnd = findNull(data, size, pos);
                if (strEnd == size_t(-1)) return msg;
                msg.storeArg<std::string>(index, reinterpret_cast<const char*>(data + pos), strEnd - pos);
                pos = skipPadding(strEnd + 1, size, paddingMissing);
                break;
            }
            case 'b': {
                if (4 > size - pos) return msg;
                uint32_t blobSize = readBe32(data + pos);
                pos += 4;
                // The blob must fit in the data left
                if (blobSize > size - pos) return msg;
                msg.storeArg<std::vector<uint8_t>>(index, data + pos, data + pos + blobSize);
                pos = skipPadding(pos + blobSize, size, paddingMissing);
                break;
            }
            case 'T':
            case 'F':
            case 'N':
            case 'I':
                // No data: the type tag alone is the argument
                break;
            case '[':
                ++arrayDepth;
                break;
            case ']':
                if (arrayDepth == 0) return msg;  // ']' without a '['
                --arrayDepth;
                break;
            default:
                // Not an OSC 1.0 type tag: its size is unknown
                return msg;
        }
    }
    if (arrayDepth != 0) return msg;  // a '[' without its ']'

    ok = true;
    return msg;
}

// =============================================================================
// toString - Debug string
// =============================================================================
std::string OscMessage::toString() const {
    std::ostringstream oss;
    oss << address_;

    for (size_t i = 0; i < typeTags_.size(); ++i) {
        oss << " ";
        const char type = typeTags_[i];
        const ArgVariant* stored = findArg(i);
        if (!stored) {
            // T F N I [ ] are shown as their tag
            oss << type;
            continue;
        }
        const ArgVariant& arg = *stored;

        switch (type) {
            case 'i': oss << "i:" << std::get<int32_t>(arg); break;
            case 'f': oss << "f:" << std::get<float>(arg); break;
            case 's': oss << "s:\"" << std::get<std::string>(arg) << "\""; break;
            case 'S': oss << "S:\"" << std::get<std::string>(arg) << "\""; break;
            case 'b': oss << "b:[" << std::get<std::vector<uint8_t>>(arg).size() << " bytes]"; break;
            case 'h': oss << "h:" << std::get<int64_t>(arg); break;
            case 'd': oss << "d:" << std::get<double>(arg); break;
            case 't': oss << "t:" << std::get<OscTimetag>(arg).value; break;
            case 'c': oss << "c:'" << std::get<char>(arg) << "'"; break;
            case 'r': {
                const OscRgba& c = std::get<OscRgba>(arg);
                oss << "r:" << int(c.r) << "," << int(c.g) << "," << int(c.b) << "," << int(c.a);
                break;
            }
            case 'm': {
                const OscMidi& m = std::get<OscMidi>(arg);
                oss << "m:" << int(m.port) << "," << int(m.status) << "," << int(m.data1) << "," << int(m.data2);
                break;
            }
            default:
                oss << type;
                break;
        }
    }

    return oss.str();
}

}  // namespace tcx::osc
