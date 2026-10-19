#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <snappy-c.h>

namespace tcx::hap::detail {

// Bound (security): At most 1024 chunks (16x FFmpeg's 64; Jokyo recommends 8); Overflow rejects the frame and is counted.
inline constexpr size_t kMaxHapChunks = 1024;
// Pinned HapChunkDecodeInfo is 24 bytes on ILP32, 40 on LP64 (six fields).
// 64 bytes conservatively covers it and std::thread on supported 32/64-bit ABIs.
static_assert(kMaxHapChunks <= UINT32_MAX / 64);
static_assert(kMaxHapChunks <= std::numeric_limits<size_t>::max() / 64);

struct HapSection {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t totalSize = 0;
    uint8_t type = 0;
};

inline uint32_t hapRead32(const uint8_t* data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
           (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

inline bool readHapSection(const uint8_t* data, size_t size, HapSection& section) {
    if (size < 4) return false;
    size_t length = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16);
    size_t header = 4;
    if (length == 0) {
        if (size < 8) return false;
        length = hapRead32(data + 4);
        header = 8;
    }
    // Bound (abi): Lengths stay below 0x80000000 for hap.c's signed shifts; Overflow rejects the frame and is counted.
    if (length >= UINT32_C(0x80000000) || length > size - header) return false;
    section = {data + header, length, header + length, data[3]};
    return true;
}

inline HapFormat hapSectionFormat(uint8_t type) {
    switch (type & 0x0f) {
        case 0xb: return HapFormat::DXT1;
        case 0xe: return HapFormat::DXT5;
        case 0xf: return HapFormat::YCoCgDXT5;
        case 0xc: return HapFormat::BC7;
        case 0x1: return HapFormat::RGTC1;
        default: return HapFormat::Unknown;
    }
}

inline bool hapChunkOutputSize(const uint8_t* data, size_t size, uint8_t compressor,
                               size_t remaining, size_t& decodedSize) {
    if (size == 0) return false;
    decodedSize = size;
    if (compressor == 0xb) {
        // Inspect only a bounded chunk, including its complete Snappy stream.
        if (snappy_uncompressed_length(reinterpret_cast<const char*>(data), size, &decodedSize) != SNAPPY_OK ||
            decodedSize == 0 || decodedSize > remaining) return false;
        return snappy_validate_compressed_buffer(reinterpret_cast<const char*>(data), size) == SNAPPY_OK;
    }
    return compressor == 0xa && decodedSize <= remaining;
}

inline bool validateHapTexture(const HapSection& texture, uint32_t width, uint32_t height,
                               bool checkDimensions) {
    const auto format = hapSectionFormat(texture.type);
    if (format == HapFormat::Unknown) return false;
    const size_t expected = checkDimensions ? calculateTextureSize(width, height, format) :
                                            std::numeric_limits<unsigned long>::max();
    if (expected == 0) return false;
    const uint8_t compressor = texture.type >> 4;
    size_t outputSize = 0;
    if (compressor != 0xc) {
        if (!hapChunkOutputSize(texture.data, texture.size, compressor, expected, outputSize)) return false;
        return !checkDimensions || outputSize == expected;
    }

    HapSection instructions;
    if (!readHapSection(texture.data, texture.size, instructions) || instructions.type != 1) return false;
    HapSection compressors, sizes, offsets;
    size_t position = 0;
    while (position < instructions.size) {
        HapSection table;
        if (!readHapSection(instructions.data + position, instructions.size - position, table)) return false;
        HapSection* destination = nullptr;
        switch (table.type) {
            case 2: destination = &compressors; break;
            case 3: destination = &sizes; break;
            case 4: destination = &offsets; break;
            default: break; // Unknown instruction sections are skipped by hap.c.
        }
        if (destination) {
            if (table.size == 0 || destination->data) return false;
            *destination = table;
        }
        position += table.totalSize;
    }
    // Bound (abi): Last table index (count - 1) * 4 must fit hap.c's int; Overflow rejects the frame and is counted.
    if (compressors.size > size_t(std::numeric_limits<int>::max() / 4) + 1) return false;
    if (compressors.size > kMaxHapChunks) return false;
    if (!compressors.data || !sizes.data || sizes.size % 4 != 0 ||
        sizes.size / 4 != compressors.size ||
        (offsets.data && (offsets.size % 4 != 0 || offsets.size / 4 != compressors.size))) return false;

    const uint8_t* data = texture.data + instructions.totalSize;
    const size_t dataSize = texture.size - instructions.totalSize;
    size_t inputSize = 0;
    for (size_t i = 0; i < compressors.size; ++i) {
        const size_t size = hapRead32(sizes.data + i * 4);
        const size_t offset = offsets.data ? hapRead32(offsets.data + i * 4) : inputSize;
        // Explicit offsets may overlap or repeat ranges. Implicit offsets use the
        // cumulative input size, so the same range check also contains their sum.
        if (offset > dataSize || size > dataSize - offset ||
            size > std::numeric_limits<size_t>::max() - inputSize) return false;
        size_t decodedSize = 0;
        if (!hapChunkOutputSize(data + offset, size, compressors.data[i], expected - outputSize, decodedSize)) return false;
        inputSize += size;
        outputSize += decodedSize;
    }
    return !checkDimensions || outputSize == expected;
}

// Validate every texture before the reference decoder forms any chunk pointers.
inline bool validateHapFrame(const uint8_t* data, size_t size, uint32_t width, uint32_t height,
                             HapFormat& firstFormat, bool checkDimensions = true) {
    // Bound (abi): Frame bytes must fit hap.c's uint32_t lengths; Overflow rejects the frame and is counted.
    if (!data || size > std::numeric_limits<uint32_t>::max()) return false;
    HapSection frame;
    if (!readHapSection(data, size, frame)) return false;
    if (frame.type != 0xd) {
        firstFormat = hapSectionFormat(frame.type);
        return validateHapTexture(frame, width, height, checkDimensions);
    }
    size_t position = 0;
    unsigned int count = 0;
    while (position < frame.size) {
        HapSection texture;
        // Bound (format): HAP containers permit at most two textures; Overflow rejects the frame and is counted.
        if (++count > 2 || !readHapSection(frame.data + position, frame.size - position, texture) ||
            !validateHapTexture(texture, width, height, checkDimensions)) return false;
        if (count == 1) firstFormat = hapSectionFormat(texture.type);
        position += texture.totalSize;
    }
    return count > 0;
}

} // namespace tcx::hap::detail
