#pragma once

// =============================================================================
// tcDepthRecordFormat.h - .tcdc on-disk format for depth recordings
// =============================================================================
//
// A simple, seekable container for canonical DepthFrames:
//
//   [FileHeader]                       magic/version, resolution, intrinsics,
//                                      stream flags, codec ids, frameCount,
//                                      indexOffset (patched on close)
//   [Frame] x N                        each independently (de)compressed (intra),
//                                      so any frame is a seek target
//   [Index] @ indexOffset              { double timestamp; uint64 offset } x N
//
// Streams are compressed per-frame: depth via hi/lo byte-plane split + LZ4
// (HiloLZ4), color via LZ4 over RGBA. All lossless. Codecs are tagged in the
// header so others (QOI/JPEG-XS/zstd ...) can be added without a format change.
//
// Endianness: little-endian host assumed (no byte-swap in v1).
//
// =============================================================================

#include <tcxDepthCamera.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace tcx {

using namespace tc;

// Codec ids stored in the header (a value works only if the reader supports it).
enum class DepthCodecId : std::uint8_t { Raw = 0, LZ4 = 1, HiloLZ4 = 2 };
enum class ColorCodecId : std::uint8_t { Raw = 0, LZ4 = 1 };  // QOI/JPEG-XS later

// Stream presence bits (FileHeader.streamFlags and the per-frame freshness byte).
enum : std::uint8_t { STREAM_DEPTH = 1, STREAM_COLOR = 2, STREAM_INFRARED = 4 };

#pragma pack(push, 1)
struct TcdcHeader {
    char          magic[4];      // 'T','C','D','C'
    std::uint32_t version;
    std::uint8_t  streamFlags;   // STREAM_*
    std::uint8_t  depthCodec;    // DepthCodecId
    std::uint8_t  colorCodec;    // ColorCodecId
    std::uint8_t  sensorType;    // DepthSensorType
    std::int32_t  width;
    std::int32_t  height;
    float         depthScale;
    // intrinsics
    std::int32_t  inWidth;
    std::int32_t  inHeight;
    float fx, fy, cx, cy, k1, k2, k3, p1, p2;
    std::uint32_t frameCount;    // patched on stop()
    std::uint64_t indexOffset;   // patched on stop() (0 = no index)
};
#pragma pack(pop)

constexpr std::uint32_t TCDC_VERSION = 1;

namespace tcd_detail {

inline TcdcHeader makeHeader(const DepthFrame& f, std::uint8_t streamFlags,
                             DepthCodecId dc, ColorCodecId cc,
                             DepthSensorType sensor) {
    TcdcHeader h{};
    h.magic[0] = 'T'; h.magic[1] = 'C'; h.magic[2] = 'D'; h.magic[3] = 'C';
    h.version = TCDC_VERSION;
    h.streamFlags = streamFlags;
    h.depthCodec = static_cast<std::uint8_t>(dc);
    h.colorCodec = static_cast<std::uint8_t>(cc);
    h.sensorType = static_cast<std::uint8_t>(sensor);
    h.width = f.w;
    h.height = f.h;
    h.depthScale = f.depthScale;
    const DepthIntrinsics& in = f.intrinsics;
    h.inWidth = in.width; h.inHeight = in.height;
    h.fx = in.fx; h.fy = in.fy; h.cx = in.cx; h.cy = in.cy;
    h.k1 = in.k1; h.k2 = in.k2; h.k3 = in.k3; h.p1 = in.p1; h.p2 = in.p2;
    h.frameCount = 0;
    h.indexOffset = 0;
    return h;
}

inline void applyIntrinsics(const TcdcHeader& h, DepthFrame& f) {
    f.w = h.width; f.h = h.height; f.depthScale = h.depthScale;
    DepthIntrinsics& in = f.intrinsics;
    in.width = h.inWidth; in.height = h.inHeight;
    in.fx = h.fx; in.fy = h.fy; in.cx = h.cx; in.cy = h.cy;
    in.k1 = h.k1; in.k2 = h.k2; in.k3 = h.k3; in.p1 = h.p1; in.p2 = h.p2;
}

template <class T> void wr(std::ostream& o, const T& v) {
    o.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T> bool rd(std::istream& i, T& v) {
    return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

// Write one frame's stream blocks (after the caller has recorded its offset).
inline void writeFrame(std::ostream& o, const DepthFrame& f,
                       std::uint8_t streamFlags, DepthCodecId dc, ColorCodecId cc,
                       std::vector<std::uint8_t>& scratch,
                       std::vector<std::uint8_t>& comp) {
    wr(o, f.timestamp);
    wr<std::uint8_t>(o, streamFlags);

    if (streamFlags & STREAM_DEPTH) {
        const std::uint32_t n = static_cast<std::uint32_t>(f.depth.size());
        const void* src; std::size_t srcBytes; Codec codec;
        if (dc == DepthCodecId::HiloLZ4) {
            scratch.resize(static_cast<std::size_t>(n) * 2);
            for (std::uint32_t i = 0; i < n; ++i) {
                scratch[i]     = static_cast<std::uint8_t>(f.depth[i] >> 8);
                scratch[n + i] = static_cast<std::uint8_t>(f.depth[i] & 0xff);
            }
            src = scratch.data(); srcBytes = static_cast<std::size_t>(n) * 2; codec = Codec::LZ4;
        } else {
            src = f.depth.data(); srcBytes = static_cast<std::size_t>(n) * 2;
            codec = (dc == DepthCodecId::LZ4) ? Codec::LZ4 : Codec::None;
        }
        compress(src, srcBytes, comp, codec);
        wr<std::uint32_t>(o, n);
        wr<std::uint32_t>(o, static_cast<std::uint32_t>(srcBytes));
        wr<std::uint32_t>(o, static_cast<std::uint32_t>(comp.size()));
        o.write(reinterpret_cast<const char*>(comp.data()), comp.size());
    }

    if (streamFlags & STREAM_COLOR) {
        const Pixels& c = f.color;
        const std::int32_t cw = c.getWidth(), ch = c.getHeight();
        const std::uint8_t chn = static_cast<std::uint8_t>(c.getChannels());
        const std::size_t rawBytes = static_cast<std::size_t>(cw) * ch * chn;
        const Codec codec = (cc == ColorCodecId::LZ4) ? Codec::LZ4 : Codec::None;
        compress(c.getData(), rawBytes, comp, codec);
        wr<std::int32_t>(o, cw);
        wr<std::int32_t>(o, ch);
        wr<std::uint8_t>(o, chn);
        wr<std::uint32_t>(o, static_cast<std::uint32_t>(rawBytes));
        wr<std::uint32_t>(o, static_cast<std::uint32_t>(comp.size()));
        o.write(reinterpret_cast<const char*>(comp.data()), comp.size());
    }
}

// Read one frame into dst (intrinsics/resolution taken from the header).
inline bool readFrame(std::istream& in, const TcdcHeader& h, DepthFrame& dst,
                      std::vector<std::uint8_t>& scratch) {
    if (!rd(in, dst.timestamp)) return false;
    std::uint8_t fresh = 0;
    if (!rd(in, fresh)) return false;
    applyIntrinsics(h, dst);
    dst.world.clear();

    if (fresh & STREAM_DEPTH) {
        std::uint32_t n = 0, rawBytes = 0, compSize = 0;
        rd(in, n); rd(in, rawBytes); rd(in, compSize);
        scratch.resize(compSize);
        in.read(reinterpret_cast<char*>(scratch.data()), compSize);
        if (h.depthCodec == static_cast<std::uint8_t>(DepthCodecId::HiloLZ4)) {
            std::vector<std::uint8_t> planes(rawBytes);
            decompress(scratch.data(), compSize, planes.data(), rawBytes, Codec::LZ4);
            dst.depth.resize(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                dst.depth[i] = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(planes[i]) << 8) | planes[n + i]);
            }
        } else {
            const Codec codec = (h.depthCodec == static_cast<std::uint8_t>(DepthCodecId::LZ4))
                                ? Codec::LZ4 : Codec::None;
            dst.depth.resize(n);
            decompress(scratch.data(), compSize, dst.depth.data(), rawBytes, codec);
        }
    } else {
        dst.depth.clear();
    }

    if (fresh & STREAM_COLOR) {
        std::int32_t cw = 0, ch = 0; std::uint8_t chn = 0;
        std::uint32_t rawBytes = 0, compSize = 0;
        rd(in, cw); rd(in, ch); rd(in, chn); rd(in, rawBytes); rd(in, compSize);
        scratch.resize(compSize);
        in.read(reinterpret_cast<char*>(scratch.data()), compSize);
        if (!dst.color.isAllocated() || dst.color.getWidth() != cw ||
            dst.color.getHeight() != ch || dst.color.getChannels() != chn) {
            dst.color.allocate(cw, ch, chn);
        }
        const Codec codec = (h.colorCodec == static_cast<std::uint8_t>(ColorCodecId::LZ4))
                            ? Codec::LZ4 : Codec::None;
        decompress(scratch.data(), compSize, dst.color.getData(), rawBytes, codec);
    } else if (dst.color.isAllocated()) {
        dst.color = Pixels{};
    }

    return true;
}

} // namespace tcd_detail
} // namespace tcx
