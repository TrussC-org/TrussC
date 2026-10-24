#pragma once

#include "tcxOscBundle.h"

namespace tcx::osc::osc_internal {

struct PacketPreflight {
    // Bound (security): aggregate decoded elements and tags amplify packet storage/work. Overflow rejects the whole outer packet and is counted by the receiver.
    static constexpr size_t workLimit = 4096;
    // Bound (platform): OSC datagrams fit the UDP receive buffer. Overflow rejects the whole packet and is counted by the receiver.
    static constexpr size_t byteLimit = 65536;
    size_t work = 0;
    size_t limit = 0;
    size_t needed = 0;
    const char* units = nullptr;

    bool addWork(size_t count) {
        if (count > workLimit - work) {
            limit = workLimit;
            needed = work + count;
            units = "work units";
            return false;
        }
        work += count;
        return true;
    }
    // No strings, vectors or decoded objects are allocated during this walk.
    bool scan(const uint8_t* data, size_t size, int depth = 1) {
        if (size > byteLimit) {
            limit = byteLimit; needed = size; units = "bytes";
            return false;
        }
        if (!data || size < 4) return false;
        if (OscBundle::isBundle(data, size)) {
            // Bound (security): nesting consumes stack space. Overflow rejects the whole outer packet and is counted by the receiver.
            if (depth > OscBundle::MAX_NESTING_DEPTH) {
                limit = OscBundle::MAX_NESTING_DEPTH; needed = depth; units = "bundle levels";
                return false;
            }
            if (size < 16) return false;
            size_t pos = 16;
            while (pos < size) {
                if (size - pos < 4) return false;
                uint32_t length;
                std::memcpy(&length, data + pos, 4);
                length = fromBigEndian(length);
                pos += 4;
                if (length > size - pos) return false;
                if (!addWork(1) || !scan(data + pos, length, depth + 1)) return false;
                pos += length;
            }
            return true;
        }
        if (*data != '/') return false;
        size_t addressEnd = findNull(data, size, 0);
        if (addressEnd == size_t(-1)) return false;
        size_t pos = addressEnd + 1;
        const size_t padding = (4 - (pos & 3)) & 3;
        pos += (padding < size - pos ? padding : size - pos);
        if (pos == size || data[pos] != ',') return true;
        const size_t tagEnd = findNull(data, size, pos + 1);
        return tagEnd != size_t(-1) && addWork(tagEnd - pos - 1);
    }
};

} // namespace tcx::osc::osc_internal
