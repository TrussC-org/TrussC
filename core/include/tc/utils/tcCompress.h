#pragma once

// =============================================================================
// tcCompress.h - General byte-buffer compression utility
// =============================================================================
//
// A thin, codec-tagged compression helper used across TrussC and its addons
// (depth recording, tcv video, ...). The codec is always passed explicitly
// (no default) so call sites are unambiguous.
//
//   std::vector<uint8_t> out;
//   tc::compress(depth.data(), depth.size() * sizeof(uint16_t), out, tc::Codec::LZ4);
//   ...
//   std::vector<uint8_t> back;
//   tc::decompress(out.data(), out.size(), back, originalByteCount, tc::Codec::LZ4);
//
// Reuse `out` across calls (e.g. per recorded frame) to avoid reallocation.
//
// Codecs are added as backends become available; a value works only if its
// backend is compiled in. Currently: None (verbatim) and LZ4 (lossless block).
//
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace trussc {

enum class Codec {
    None,  // store verbatim (identity copy) - the "no compression" option
    LZ4,   // LZ4 block compression (fast, lossless)
};

// Compress `nbytes` from `src` into `out` (resized to the compressed size).
// Returns false on failure (and clears `out`).
bool compress(const void* src, std::size_t nbytes,
              std::vector<std::uint8_t>& out, Codec codec);

// Decompress `nbytes` from `src` into `out`. `decompressedSize` is the known
// original byte count (store it next to the compressed data). Returns false on
// failure or size mismatch (and clears `out`).
bool decompress(const void* src, std::size_t nbytes,
                std::vector<std::uint8_t>& out, std::size_t decompressedSize,
                Codec codec);

} // namespace trussc
