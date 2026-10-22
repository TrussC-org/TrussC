// =============================================================================
// tcCompress.cpp - tc::compress / tc::decompress implementation
// =============================================================================

#include "tc/utils/tcCompress.h"
#include "tc/utils/tcLog.h"
#include "tc/utils/tcUtils.h"

#include "lz4/lz4.h"

#include <algorithm>
#include <cstring>
#include <climits>
#include <cstdio>

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
    // Bound (abi): input lengths and the return value are int. Overflow rejects the whole unit and is counted by the returned -1; LZ4 destination capacity is clamped to INT_MAX.
    if (nbytes > INT_MAX ||
        (nbytes && !src) || (dstCapacity && !dst)) return -1;
    switch (codec) {
        case Codec::None: {
            if (dstCapacity < nbytes) return -1;
            if (nbytes) std::memcpy(dst, src, nbytes);
            return static_cast<int>(nbytes);
        }
        case Codec::LZ4: {
            const int n = LZ4_decompress_safe(
                static_cast<const char*>(src), static_cast<char*>(dst),
                static_cast<int>(nbytes), static_cast<int>(std::min(dstCapacity, static_cast<std::size_t>(INT_MAX))));
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

// --- vector form -----------------------------------------------------------

namespace {
// Walk the block without producing bytes. Lengths and back references must
// fit the supplied block and declared output before any output allocation.
bool lz4OutputSize(const void* src, size_t size, size_t expected) {
    const auto* bytes = static_cast<const uint8_t*>(src);
    size_t position = 0, produced = 0;
    auto length = [&](size_t initial, size_t& value) {
        value = initial;
        if (initial == 15) {
            unsigned extension;
            do {
                if (position == size) return false;
                extension = bytes[position++];
                if (extension > expected || value > expected - extension) return false;
                value += extension;
            } while (extension == 255);
        }
        return value <= expected - produced;
    };
    while (position < size) {
        unsigned token = bytes[position++];
        size_t literals;
        if (!length(token >> 4, literals) || literals > size - position) return false;
        position += literals;
        produced += literals;
        // LZ4 block end conditions (lz4.c: LASTLITERALS == 5): the
        // final sequence is literal-only and needs five literals, except
        // for a short block consisting entirely of literals (including empty).
        if (position == size)
            return produced == expected && literals >= std::min(expected, size_t(5));
        // lz4.c: MFLIMIT == 12. Every match, including the last, must
        // start at least twelve decoded bytes before the end of the block.
        if (expected - produced < 12) return false;
        if (size - position < 2) return false;
        size_t offset = bytes[position] | (size_t(bytes[position + 1]) << 8);
        position += 2;
        if (!offset || offset > produced) return false;
        size_t match;
        if (!length(token & 15, match) || expected - produced < 4 ||
            match > expected - produced - 4) return false;
        produced += match + 4;
    }
    return false;
}
}

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
    auto failure = [&]() {
        std::vector<std::uint8_t>().swap(out);
        try {
            getLogger().log(LogLevel::Error, "[Compress] Cannot decompress complete buffer (size, format or allocation failure)");
        } catch (...) {
            std::fputs("[ERROR] [Compress] Cannot decompress complete buffer (size, format or allocation failure)\n", stderr); // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
        }
        return false;
    };
    // Bound (platform): vector storage must represent the complete result.
    // Overflow rejects the whole unit and is counted by the returned false.
    if (decompressedSize > out.max_size() || (nbytes && !src)) return failure();
    if (codec == Codec::None) {
        if (nbytes != decompressedSize) return failure();
    } else if (codec == Codec::LZ4) {
        // Bound (abi): LZ4 lengths and its decoded result use int.
        // Overflow rejects the whole unit and is counted by the returned false.
        if (decompressedSize > INT_MAX || nbytes > INT_MAX) return failure();
        if (!lz4OutputSize(src, nbytes, decompressedSize)) return failure();
    } else return failure();
    if (codec == Codec::LZ4 && decompressedSize > 256u*1024u*1024u) {
        try {
            getLogger().log(LogLevel::Notice, "[Compress] Decompressing a large local buffer (over 256 MiB)");
        } catch (...) {
            std::fputs("[NOTICE] [Compress] Decompressing a large local buffer (over 256 MiB)\n", stderr); // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
        }
    }
    try {
        // A reused vector can geometrically grow past the admitted size.
        // Failure already clears its value, so release old storage first and
        // grow from empty: no simultaneous old output or unchecked slack.
        if (out.capacity() < decompressedSize)
            std::vector<std::uint8_t>().swap(out);
        out.resize(decompressedSize);
    } catch (const std::exception&) {
        return failure();
    }
    if (codec == Codec::None) {
        // Identity lengths are size_t throughout; the raw int result must
        // not impose a codec representation limit on this vector overload.
        if (nbytes) std::memcpy(out.data(), src, nbytes);
        return true;
    }
    const int n = decompress(src, nbytes, out.data(), decompressedSize, codec);
    if (n < 0 || static_cast<std::size_t>(n) != decompressedSize) {
        return failure();
    }
    return true;
}

} // namespace trussc
