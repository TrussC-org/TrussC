#pragma once

#include "tcxOscBundle.h"

namespace tcx::osc::osc_internal {

struct PacketPreflight {
    // Bound (platform): OSC datagrams fit the UDP receive buffer. Overflow rejects the whole packet and is counted by the receiver.
    static constexpr size_t byteLimit = 65536;
    size_t limit = 0;
    size_t needed = 0;
    const char* units = nullptr;

    // Walks the whole packet without decoding it: no strings or decoded
    // objects are allocated. Nested bundles are walked with a heap stack of
    // (bundle bytes, position) pairs, not recursion, so depth does not cost
    // call-stack space; the stack never holds more than the packet's nesting
    // (a bundle needs at least 20 bytes, so under 3300 entries for 64 KiB).
    bool scan(const uint8_t* data, size_t size) {
        if (size > byteLimit) {
            limit = byteLimit; needed = size; units = "bytes";
            return false;
        }
        if (!scanLeaf(data, size)) return false;
        if (!OscBundle::isBundle(data, size)) return true;
        struct Open { const uint8_t* data; size_t size; size_t pos; };
        std::vector<Open> stack;
        stack.push_back({data, size, 16});
        while (!stack.empty()) {
            Open& top = stack.back();
            if (top.pos >= top.size) {
                stack.pop_back();
                continue;
            }
            if (top.size - top.pos < 4) return false;
            uint32_t length;
            std::memcpy(&length, top.data + top.pos, 4);
            length = fromBigEndian(length);
            top.pos += 4;
            if (length > top.size - top.pos) return false;
            const uint8_t* elementData = top.data + top.pos;
            top.pos += length;   // before a push_back below invalidates `top`
            if (!scanLeaf(elementData, length)) return false;
            if (OscBundle::isBundle(elementData, length)) stack.push_back({elementData, length, 16});
        }
        return true;
    }

private:
    // One packet or element on its own: a bundle must have its 16-byte
    // header; a message must have a well-formed address and type tag string.
    // The elements of a bundle are walked by scan().
    bool scanLeaf(const uint8_t* data, size_t size) {
        if (!data || size < 4) return false;
        if (OscBundle::isBundle(data, size)) return size >= 16;
        if (*data != '/') return false;
        size_t addressEnd = findNull(data, size, 0);
        if (addressEnd == size_t(-1)) return false;
        size_t pos = addressEnd + 1;
        const size_t padding = (4 - (pos & 3)) & 3;
        pos += (padding < size - pos ? padding : size - pos);
        if (pos == size || data[pos] != ',') return true;
        const size_t tagEnd = findNull(data, size, pos + 1);
        return tagEnd != size_t(-1);
    }
};

} // namespace tcx::osc::osc_internal
