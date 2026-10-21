#pragma once

// =============================================================================
// tcCompress.h - General byte-buffer compression utility
// =============================================================================
//
// A thin, codec-tagged compression helper used across TrussC and its addons
// (depth recording, tcv video, ...). The codec is always passed explicitly
// (no default) so call sites are unambiguous.
//
// Two forms:
//
//   * vector form  - resizes an output std::vector for you. Easiest; reuse the
//     vector across calls (e.g. per recorded frame) to avoid reallocation.
//
//       std::vector<uint8_t> out;
//       tc::compress(depth.data(), depth.size()*sizeof(uint16_t), out, tc::Codec::LZ4);
//       std::vector<uint8_t> back;
//       tc::decompress(out.data(), out.size(), back, originalByteCount, tc::Codec::LZ4);
//
//   * raw form     - writes into a caller-provided buffer (dst, dstCapacity) and
//     returns the number of bytes written (or -1 on failure). Use this when you
//     need to write into a *region* of an existing buffer rather than a fresh
//     vector - e.g. splitting a frame into independently (de)compressed CHUNKS
//     and running them in PARALLEL, where chunk i decompresses straight into
//     `dst + offset_i`:
//
//       int n = tc::decompress(comp + inOffset, inSize,
//                              out.data() + outOffset, outSize, tc::Codec::LZ4);
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

// --- vector form -------------------------------------------------------------

// Compress `nbytes` from `src` into `out` (resized to the compressed size).
// Returns false on failure (and clears `out`).
bool compress(const void* src, std::size_t nbytes,
              std::vector<std::uint8_t>& out, Codec codec);

// Decompress `nbytes` from `src` into `out`. `decompressedSize` is the known
// original byte count (store it next to the compressed data). Returns false on
// failure or size mismatch (and clears `out`). Checks the encoded size before
// allocation; LZ4 output over 256 MiB emits one Notice. There is no output
// policy cap. LZ4 lengths and results must fit int; identity copies use size_t.
// Failure releases storage.
bool decompress(const void* src, std::size_t nbytes,
                std::vector<std::uint8_t>& out, std::size_t decompressedSize,
                Codec codec);

// --- raw form (write into a caller buffer; for chunked / in-place use) --------

// Worst-case compressed size for `nbytes` with `codec`. Use it to size the
// `dst` buffer before calling the raw compress() form.
std::size_t compressBound(std::size_t nbytes, Codec codec);

// Compress `nbytes` from `src` into `dst` (capacity `dstCapacity`). Returns the
// number of bytes written, or -1 on failure (e.g. dst too small). Lets you
// compress into a region of a larger buffer / run chunks in parallel.
int compress(const void* src, std::size_t nbytes,
             void* dst, std::size_t dstCapacity, Codec codec);

// Decompress `nbytes` from `src` into `dst` (capacity `dstCapacity`). Returns
// the number of bytes written, or -1 on failure. Can write straight into a
// region of an existing buffer (`dst + offset`), which is what enables
// independent, parallel per-chunk decompression.
int decompress(const void* src, std::size_t nbytes,
               void* dst, std::size_t dstCapacity, Codec codec);

} // namespace trussc
