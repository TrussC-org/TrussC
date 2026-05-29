// =============================================================================
// tcCompress.cpp - tc::compress / tc::decompress implementation
// =============================================================================

#include "tc/utils/tcCompress.h"

#include "lz4/lz4.h"

#include <cstring>

namespace trussc {

bool compress(const void* src, std::size_t nbytes,
              std::vector<std::uint8_t>& out, Codec codec) {
    switch (codec) {
        case Codec::None: {
            out.resize(nbytes);
            if (nbytes) std::memcpy(out.data(), src, nbytes);
            return true;
        }
        case Codec::LZ4: {
            if (nbytes > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) {
                out.clear();
                return false;
            }
            const int bound = LZ4_compressBound(static_cast<int>(nbytes));
            if (bound <= 0) { out.clear(); return false; }
            out.resize(static_cast<std::size_t>(bound));
            const int n = LZ4_compress_default(
                static_cast<const char*>(src),
                reinterpret_cast<char*>(out.data()),
                static_cast<int>(nbytes), bound);
            if (n <= 0) { out.clear(); return false; }
            out.resize(static_cast<std::size_t>(n));
            return true;
        }
    }
    out.clear();
    return false;
}

bool decompress(const void* src, std::size_t nbytes,
                std::vector<std::uint8_t>& out, std::size_t decompressedSize,
                Codec codec) {
    switch (codec) {
        case Codec::None: {
            out.resize(nbytes);
            if (nbytes) std::memcpy(out.data(), src, nbytes);
            return true;
        }
        case Codec::LZ4: {
            out.resize(decompressedSize);
            const int n = LZ4_decompress_safe(
                static_cast<const char*>(src),
                reinterpret_cast<char*>(out.data()),
                static_cast<int>(nbytes),
                static_cast<int>(decompressedSize));
            if (n < 0 || static_cast<std::size_t>(n) != decompressedSize) {
                out.clear();
                return false;
            }
            return true;
        }
    }
    out.clear();
    return false;
}

} // namespace trussc
