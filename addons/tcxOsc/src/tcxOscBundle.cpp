#include "tcxOscBundle.h"

namespace tcx::osc {

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
    return fromBytesAtDepth(data, size, ok, 1);
}

OscBundle OscBundle::fromBytesAtDepth(const uint8_t* data, size_t size, bool& ok, int depth) {
    ok = false;
    OscBundle bundle;

    // Nesting limit (the outermost bundle is level 1)
    if (depth > MAX_NESTING_DEPTH) return bundle;

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

    // Read elements: each is a 4-byte size, then that many bytes. The bundle
    // parses only if every element is there in full. pos <= size holds
    // throughout, and each check subtracts from size instead of adding to pos,
    // so none of them can wrap when size_t is 32 bits.
    while (pos < size) {
        // Element size (a size field cut short is a truncated element)
        if (size - pos < 4) return OscBundle();
        uint32_t sizeBe;
        std::memcpy(&sizeBe, data + pos, 4);
        uint32_t elementSize = fromBigEndian(sizeBe);
        pos += 4;

        // An element larger than the data left is truncated
        if (elementSize > size - pos) return OscBundle();

        const uint8_t* elementData = data + pos;

        // Determine if bundle or message
        if (isBundle(elementData, elementSize)) {
            bool elementOk = false;
            OscBundle childBundle = fromBytesAtDepth(elementData, elementSize, elementOk, depth + 1);
            // A nested bundle that fails rejects this bundle too, so the
            // caller sees one parse error instead of a partial bundle.
            if (!elementOk) return OscBundle();
            bundle.elements_.emplace_back(std::move(childBundle));
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

}  // namespace tcx::osc
