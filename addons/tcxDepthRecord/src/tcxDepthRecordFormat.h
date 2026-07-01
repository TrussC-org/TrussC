#pragma once

// =============================================================================
// tcxDepthRecordFormat.h - .tcdc on-disk format for depth recordings
// =============================================================================
//
// TCDC = "TrussC Depth Container" — a TLV container that can hold depth, color,
// and (via addon block types) anything else.
//
//   [Header]    magic/version, resolution, intrinsics, depth->color extrinsic,
//               codec ids, a STREAM MANIFEST (which block types the file
//               contains), frameCount, indexOffset (patched on close)
//   [Frame] x N timestamp + a sequence of TLV blocks (Type/Length/Value),
//               read until the next frame's offset (from the index)
//   [Index] x N { timestamp, offset }
//
// Blocks are TLV: { uint8 type, uint32 length, <length bytes> }. A reader parses
// the block types it knows (depth/color/ir) and SKIPS any it doesn't by `length`
// - so addons can add their own block types (>= BLOCK_CUSTOM_BASE, e.g. body /
// hand tracking) and an official player still plays depth/color, ignoring them.
//
// The header manifest lists every block type present in the file, so a reader
// can tell at a glance what's inside (depth-only? has IR? contains unknown
// streams?) without scanning frames.
//
// Compression: depth = hi/lo byte split + LZ4 (HiloLZ4), color/ir = LZ4. All
// lossless, via core tc::compress. Little-endian host assumed.
//
// =============================================================================

#include <tcxDepthCamera.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace tcx::depthrecord {

using namespace tc;

// Official block types. Addon/custom blocks use >= BLOCK_CUSTOM_BASE.
enum BlockType : std::uint8_t {
    BLOCK_DEPTH    = 1,
    BLOCK_COLOR    = 2,
    BLOCK_INFRARED = 3,
};
constexpr std::uint8_t BLOCK_CUSTOM_BASE = 0x80;

enum class DepthCodecId : std::uint8_t { Raw = 0, LZ4 = 1, HiloLZ4 = 2 };
enum class ColorCodecId : std::uint8_t { Raw = 0, LZ4 = 1 };

constexpr std::uint32_t TCDC_VERSION = 2;   // v2: TLV blocks + stream manifest
constexpr int TCDC_MAX_STREAM_TYPES = 31;

#pragma pack(push, 1)
struct TcdcHeader {
    char          magic[4];      // 'T','C','D','C'
    std::uint32_t version;
    std::uint8_t  depthCodec;    // DepthCodecId
    std::uint8_t  colorCodec;    // ColorCodecId
    std::uint8_t  sensorType;    // DepthSensorType
    std::uint8_t  reserved;
    std::int32_t  width;
    std::int32_t  height;
    float         depthScale;
    // depth intrinsics
    std::int32_t  inWidth, inHeight;
    float fx, fy, cx, cy, k1, k2, k3, p1, p2;
    // color intrinsics (native color res) + depth->color extrinsic (row-major)
    std::int32_t  cinWidth, cinHeight;
    float cfx, cfy, ccx, ccy, ck1, ck2, ck3, cp1, cp2;
    float depthToColor[16];
    // manifest: block types present in the file (patched on stop)
    std::uint8_t  streamTypeCount;
    std::uint8_t  streamTypes[TCDC_MAX_STREAM_TYPES];
    std::uint32_t frameCount;    // patched on stop
    std::uint64_t indexOffset;   // patched on stop (0 = no index)
};
#pragma pack(pop)

namespace tcd_detail {

template <class T> void wr(std::ostream& o, const T& v) {
    o.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T> bool rd(std::istream& i, T& v) {
    return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

inline TcdcHeader makeHeader(const DepthFrame& f, DepthCodecId dc, ColorCodecId cc,
                             DepthSensorType sensor) {
    TcdcHeader h{};
    h.magic[0]='T'; h.magic[1]='C'; h.magic[2]='D'; h.magic[3]='C';
    h.version = TCDC_VERSION;
    h.depthCodec = static_cast<std::uint8_t>(dc);
    h.colorCodec = static_cast<std::uint8_t>(cc);
    h.sensorType = static_cast<std::uint8_t>(sensor);
    h.width = f.w; h.height = f.h; h.depthScale = f.depthScale;
    const DepthIntrinsics& in = f.intrinsics;
    h.inWidth=in.width; h.inHeight=in.height;
    h.fx=in.fx; h.fy=in.fy; h.cx=in.cx; h.cy=in.cy;
    h.k1=in.k1; h.k2=in.k2; h.k3=in.k3; h.p1=in.p1; h.p2=in.p2;
    const DepthIntrinsics& ci = f.colorIntrinsics;
    h.cinWidth=ci.width; h.cinHeight=ci.height;
    h.cfx=ci.fx; h.cfy=ci.fy; h.ccx=ci.cx; h.ccy=ci.cy;
    h.ck1=ci.k1; h.ck2=ci.k2; h.ck3=ci.k3; h.cp1=ci.p1; h.cp2=ci.p2;
    for (int i=0;i<16;++i) h.depthToColor[i]=f.depthToColor.m[i];
    return h;
}

inline void applyHeader(const TcdcHeader& h, DepthFrame& f) {
    f.w=h.width; f.h=h.height; f.depthScale=h.depthScale;
    DepthIntrinsics& in=f.intrinsics;
    in.width=h.inWidth; in.height=h.inHeight;
    in.fx=h.fx; in.fy=h.fy; in.cx=h.cx; in.cy=h.cy;
    in.k1=h.k1; in.k2=h.k2; in.k3=h.k3; in.p1=h.p1; in.p2=h.p2;
    DepthIntrinsics& ci=f.colorIntrinsics;
    ci.width=h.cinWidth; ci.height=h.cinHeight;
    ci.fx=h.cfx; ci.fy=h.cfy; ci.cx=h.ccx; ci.cy=h.ccy;
    ci.k1=h.ck1; ci.k2=h.ck2; ci.k3=h.ck3; ci.p1=h.cp1; ci.p2=h.cp2;
    for (int i=0;i<16;++i) f.depthToColor.m[i]=h.depthToColor[i];
}

// --- block writers (each writes one TLV block) -------------------------------

inline void writeDepthBlock(std::ostream& o, const DepthFrame& f, DepthCodecId dc,
                            std::vector<std::uint8_t>& scratch,
                            std::vector<std::uint8_t>& comp) {
    const std::uint32_t n = static_cast<std::uint32_t>(f.depth.size());
    const void* src; std::size_t srcBytes; Codec codec;
    if (dc == DepthCodecId::HiloLZ4) {
        scratch.resize(static_cast<std::size_t>(n) * 2);
        for (std::uint32_t i=0;i<n;++i) {
            scratch[i]     = static_cast<std::uint8_t>(f.depth[i] >> 8);
            scratch[n + i] = static_cast<std::uint8_t>(f.depth[i] & 0xff);
        }
        src=scratch.data(); srcBytes=static_cast<std::size_t>(n)*2; codec=Codec::LZ4;
    } else {
        src=f.depth.data(); srcBytes=static_cast<std::size_t>(n)*2;
        codec=(dc==DepthCodecId::LZ4)?Codec::LZ4:Codec::None;
    }
    compress(src, srcBytes, comp, codec);
    const std::uint32_t payloadLen = 12 + static_cast<std::uint32_t>(comp.size());
    wr<std::uint8_t>(o, BLOCK_DEPTH);
    wr<std::uint32_t>(o, payloadLen);
    wr<std::uint32_t>(o, n);
    wr<std::uint32_t>(o, static_cast<std::uint32_t>(srcBytes));
    wr<std::uint32_t>(o, static_cast<std::uint32_t>(comp.size()));
    o.write(reinterpret_cast<const char*>(comp.data()), comp.size());
}

inline void writeColorBlock(std::ostream& o, const DepthFrame& f, ColorCodecId cc,
                            std::vector<std::uint8_t>& comp) {
    const Pixels& c = f.color;
    const std::int32_t cw=c.getWidth(), ch=c.getHeight();
    const std::uint8_t chn=static_cast<std::uint8_t>(c.getChannels());
    const std::size_t rawBytes=static_cast<std::size_t>(cw)*ch*chn;
    compress(c.getData(), rawBytes, comp, (cc==ColorCodecId::LZ4)?Codec::LZ4:Codec::None);
    const std::uint32_t payloadLen = 13 + static_cast<std::uint32_t>(comp.size());
    wr<std::uint8_t>(o, BLOCK_COLOR);
    wr<std::uint32_t>(o, payloadLen);
    wr<std::int32_t>(o, cw); wr<std::int32_t>(o, ch); wr<std::uint8_t>(o, chn);
    wr<std::uint32_t>(o, static_cast<std::uint32_t>(rawBytes));
    wr<std::uint32_t>(o, static_cast<std::uint32_t>(comp.size()));
    o.write(reinterpret_cast<const char*>(comp.data()), comp.size());
}

// --- block parsers (payload already located; read exactly its bytes) ---------

inline void parseDepthPayload(std::istream& in, const TcdcHeader& h, DepthFrame& dst,
                              std::vector<std::uint8_t>& scratch) {
    std::uint32_t n=0, rawBytes=0, compSize=0;
    rd(in,n); rd(in,rawBytes); rd(in,compSize);
    scratch.resize(compSize);
    in.read(reinterpret_cast<char*>(scratch.data()), compSize);
    if (h.depthCodec == static_cast<std::uint8_t>(DepthCodecId::HiloLZ4)) {
        std::vector<std::uint8_t> planes(rawBytes);
        decompress(scratch.data(), compSize, planes.data(), rawBytes, Codec::LZ4);
        dst.depth.resize(n);
        for (std::uint32_t i=0;i<n;++i)
            dst.depth[i]=static_cast<std::uint16_t>((static_cast<std::uint16_t>(planes[i])<<8)|planes[n+i]);
    } else {
        const Codec codec=(h.depthCodec==static_cast<std::uint8_t>(DepthCodecId::LZ4))?Codec::LZ4:Codec::None;
        dst.depth.resize(n);
        decompress(scratch.data(), compSize, dst.depth.data(), rawBytes, codec);
    }
}

inline void parseColorPayload(std::istream& in, const TcdcHeader& h, DepthFrame& dst,
                              std::vector<std::uint8_t>& scratch) {
    std::int32_t cw=0, ch=0; std::uint8_t chn=0; std::uint32_t rawBytes=0, compSize=0;
    rd(in,cw); rd(in,ch); rd(in,chn); rd(in,rawBytes); rd(in,compSize);
    scratch.resize(compSize);
    in.read(reinterpret_cast<char*>(scratch.data()), compSize);
    if (!dst.color.isAllocated() || dst.color.getWidth()!=cw ||
        dst.color.getHeight()!=ch || dst.color.getChannels()!=chn) {
        dst.color.allocate(cw, ch, chn);
    }
    const Codec codec=(h.colorCodec==static_cast<std::uint8_t>(ColorCodecId::LZ4))?Codec::LZ4:Codec::None;
    decompress(scratch.data(), compSize, dst.color.getData(), rawBytes, codec);
}

} // namespace tcd_detail
} // namespace tcx::depthrecord

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::depthrecord`.
// These silent aliases keep older flat `tcx::` code compiling. DEPRECATED.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See README for migration.)
// -----------------------------------------------------------------------------
namespace tcx { // deprecated: remove at v1.0.0
    using depthrecord::BlockType;
    using depthrecord::BLOCK_DEPTH;
    using depthrecord::BLOCK_COLOR;
    using depthrecord::BLOCK_INFRARED;
    using depthrecord::BLOCK_CUSTOM_BASE;
    using depthrecord::DepthCodecId;
    using depthrecord::ColorCodecId;
    using depthrecord::TCDC_VERSION;
    using depthrecord::TCDC_MAX_STREAM_TYPES;
    using depthrecord::TcdcHeader;
}
