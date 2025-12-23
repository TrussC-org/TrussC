#include "tcxOscBundle.h"

namespace trussc {

using namespace osc_internal;

// =============================================================================
// toBytes - Serialize bundle to byte array
// =============================================================================
std::vector<uint8_t> OscBundle::toBytes() const {
    std::vector<uint8_t> result;

    // "#bundle\0"
    const char* bundleId = "#bundle";
    result.insert(result.end(), bundleId, bundleId + 8);

    // Timetag (8 bytes, big-endian)
    uint64_t be = toBigEndian64(timetag_);
    auto* p = reinterpret_cast<uint8_t*>(&be);
    result.insert(result.end(), p, p + 8);

    // Each element
    for (const auto& element : elements_) {
        std::vector<uint8_t> elementBytes;

        if (auto* msg = std::get_if<OscMessage>(&element)) {
            elementBytes = msg->toBytes();
        }
        else if (auto* bundle = std::get_if<OscBundle>(&element)) {
            elementBytes = bundle->toBytes();
        }

        // Size (4 bytes, big-endian)
        uint32_t size = static_cast<uint32_t>(elementBytes.size());
        uint32_t sizeBe = toBigEndian(size);
        auto* sp = reinterpret_cast<uint8_t*>(&sizeBe);
        result.insert(result.end(), sp, sp + 4);

        // Element data
        result.insert(result.end(), elementBytes.begin(), elementBytes.end());
    }

    return result;
}

// =============================================================================
// fromBytes - Parse bundle from byte array (robust implementation)
// =============================================================================
OscBundle OscBundle::fromBytes(const uint8_t* data, size_t size, bool& ok) {
    ok = false;
    OscBundle bundle;

    // Minimum size check: "#bundle\0" (8) + timetag (8) = 16
    if (!data || size < 16) return bundle;

    // Verify bundle ID
    if (!isBundle(data, size)) return bundle;

    size_t pos = 8;

    // Read timetag
    uint64_t be;
    std::memcpy(&be, data + pos, 8);
    bundle.timetag_ = fromBigEndian64(be);
    pos += 8;

    // Read elements
    while (pos + 4 <= size) {
        // Element size
        uint32_t sizeBe;
        std::memcpy(&sizeBe, data + pos, 4);
        uint32_t elementSize = fromBigEndian(sizeBe);
        pos += 4;

        if (pos + elementSize > size) {
            // Invalid size (not enough remaining data)
            break;
        }

        const uint8_t* elementData = data + pos;

        // Determine if bundle or message
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
