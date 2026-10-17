// =============================================================================
// tcCompress.cpp - tc::compress / tc::decompress implementation
// =============================================================================

#include "tc/utils/tcCompress.h"

#include "lz4/lz4.h"

#include <cstring>

namespace trussc {

// --- raw form (the primitives) ----------------------------------------------

int compress(const void* src, std::size_t nbytes,
             void* dst, std::size_t dstCapacity, Codec codec) {
    switch (codec) {
        case Codec::None: {
            if (dstCapacity < nbytes) return -1;
            if (nbytes) std::memcpy(dst, src, nbytes);
            return static_cast<int>(nbytes);
        }
        case Codec::LZ4: {
            // Bound (platform): the LZ4 API accepts only input within LZ4_MAX_INPUT_SIZE. Overflow returns -1.
            if (nbytes > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) return -1;
            const int n = LZ4_compress_default(
                static_cast<const char*>(src), static_cast<char*>(dst),
                static_cast<int>(nbytes), static_cast<int>(dstCapacity));
            return n > 0 ? n : -1;
        }
    }
    return -1;
}

int decompress(const void* src, std::size_t nbytes,
               void* dst, std::size_t dstCapacity, Codec codec) {
    switch (codec) {
        case Codec::None: {
            if (dstCapacity < nbytes) return -1;
            if (nbytes) std::memcpy(dst, src, nbytes);
            return static_cast<int>(nbytes);
        }
        case Codec::LZ4: {
            const int n = LZ4_decompress_safe(
                static_cast<const char*>(src), static_cast<char*>(dst),
                static_cast<int>(nbytes), static_cast<int>(dstCapacity));
            return n >= 0 ? n : -1;
        }
    }
    return -1;
}

std::size_t compressBound(std::size_t nbytes, Codec codec) {
    switch (codec) {
        case Codec::None: return nbytes;
        case Codec::LZ4:  return static_cast<std::size_t>(
                                     LZ4_compressBound(static_cast<int>(nbytes)));
    }
    return 0;
}

// --- vector form (convenience, built on the raw form) ------------------------

bool compress(const void* src, std::size_t nbytes,
              std::vector<std::uint8_t>& out, Codec codec) {
    const std::size_t cap = compressBound(nbytes, codec);
    out.resize(cap);
    const int n = compress(src, nbytes, out.data(), cap, codec);
    if (n < 0) { out.clear(); return false; }
    out.resize(static_cast<std::size_t>(n));
    return true;
}

bool decompress(const void* src, std::size_t nbytes,
                std::vector<std::uint8_t>& out, std::size_t decompressedSize,
                Codec codec) {
    out.resize(decompressedSize);
    const int n = decompress(src, nbytes, out.data(), decompressedSize, codec);
    if (n < 0 || static_cast<std::size_t>(n) != decompressedSize) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace trussc
