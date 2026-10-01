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
// Files written before the color length was fixed have a color block length 4
// bytes short (13 + compressed size instead of 17 + compressed size); a reader
// that uses a color block's length accepts that value too (see
// LEGACY_COLOR_BLOCK_FIELDS).
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

#include <climits>
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

// --- block layout ------------------------------------------------------------
//
// Bytes of size fields in front of a block's compressed data, summed from the
// fields the writers below put there. A block's length is these plus the
// compressed size.
constexpr std::uint32_t DEPTH_BLOCK_FIELDS =
    sizeof(std::uint32_t)        // sample count
    + sizeof(std::uint32_t)      // byte size
    + sizeof(std::uint32_t);     // compressed size
constexpr std::uint32_t COLOR_BLOCK_FIELDS =
    sizeof(std::int32_t) * 2     // width, height
    + sizeof(std::uint8_t)       // channels
    + sizeof(std::uint32_t)      // byte size
    + sizeof(std::uint32_t);     // compressed size
static_assert(DEPTH_BLOCK_FIELDS == 12 && COLOR_BLOCK_FIELDS == 17,
              "the .tcdc block layout changed");

// DepthRecorder used to write a color block's length as 13 + compressed size,
// one 4-byte field short. Readers accept exactly that length as well.
constexpr std::uint32_t LEGACY_COLOR_BLOCK_FIELDS = 13;

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
    const std::uint32_t payloadLen = DEPTH_BLOCK_FIELDS + static_cast<std::uint32_t>(comp.size());
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
    const std::uint32_t payloadLen = COLOR_BLOCK_FIELDS + static_cast<std::uint32_t>(comp.size());
    wr<std::uint8_t>(o, BLOCK_COLOR);
    wr<std::uint32_t>(o, payloadLen);
    wr<std::int32_t>(o, cw); wr<std::int32_t>(o, ch); wr<std::uint8_t>(o, chn);
    wr<std::uint32_t>(o, static_cast<std::uint32_t>(rawBytes));
    wr<std::uint32_t>(o, static_cast<std::uint32_t>(comp.size()));
    o.write(reinterpret_cast<const char*>(comp.data()), comp.size());
}

// --- block parsers (payload already located) ---------------------------------
//
// `len` is the block's stated length and `room` the bytes left in the frame
// after the block header. A depth/color payload ends where its own size fields
// say, as it always has: files written before the color length was fixed state
// a color block's length as 13 + compressed size, though the fields take 17
// bytes. `used` returns the bytes the payload takes, or 0 when that isn't known
// to lie within the block and the frame; the caller then can't tell where the
// next block starts.
//
// The size fields are checked against the block, the frame dimensions, each
// other and the largest size decompress() can produce BEFORE anything is
// allocated or decoded, decompress() gets the real size
// of the destination, and decoding must produce exactly the expected number of
// bytes. If a check fails the parser returns false with `why` set and leaves
// that stream empty in dst; the caller skips the block. Blocks written by
// DepthRecorder pass as long as the header's width x height (taken from the
// first recorded frame) matches the depth plane.

// decompress() takes and returns the decoded size as an int, so a byte size
// above INT_MAX can't be decoded. DepthRecorder doesn't produce a valid block
// above it: compress() fails past LZ4_MAX_INPUT_SIZE bytes (LZ4) or INT_MAX
// bytes (plain copy).
inline bool decodableSize(std::uint32_t rawBytes) {
    return rawBytes <= static_cast<std::uint32_t>(INT_MAX);
}

// Whether `compSize` bytes can decode to `rawBytes`: Codec::None is a plain
// copy, and LZ4 turns each input byte into at most 255 output bytes. This caps
// what a block's sizes can make the reader allocate.
inline bool decodedSizeFits(std::uint64_t rawBytes, std::uint64_t compSize, Codec codec) {
    return codec == Codec::None ? rawBytes == compSize : rawBytes <= compSize * 255;
}

// decompress() result: exactly `expected` bytes, or a failure.
inline bool decodedExactly(int got, std::uint64_t expected) {
    return got >= 0 && static_cast<std::uint64_t>(got) == expected;
}

inline bool parseDepthPayload(std::istream& in, const TcdcHeader& h, std::uint32_t len,
                              std::uint64_t room, DepthFrame& dst,
                              std::vector<std::uint8_t>& scratch,
                              std::uint64_t& used, const char*& why) {
    auto fail = [&](const char* reason) { dst.depth.clear(); why = reason; return false; };
    used = 0;
    std::uint32_t n=0, rawBytes=0, compSize=0;
    if (len < DEPTH_BLOCK_FIELDS || room < DEPTH_BLOCK_FIELDS)
        return fail("block is shorter than its size fields");
    if (!rd(in,n) || !rd(in,rawBytes) || !rd(in,compSize)) return fail("block is cut off");
    const std::uint64_t payloadBytes = DEPTH_BLOCK_FIELDS + static_cast<std::uint64_t>(compSize);
    if (payloadBytes > len || payloadBytes > room)
        return fail("compressed size runs past the block");
    used = payloadBytes;
    const bool hilo = h.depthCodec == static_cast<std::uint8_t>(DepthCodecId::HiloLZ4);
    const Codec codec = (hilo || h.depthCodec == static_cast<std::uint8_t>(DepthCodecId::LZ4))
                            ? Codec::LZ4 : Codec::None;
    // One uint16 sample per pixel of the frame (header width x height).
    const std::uint64_t pixels = (h.width > 0 && h.height > 0)
        ? static_cast<std::uint64_t>(h.width) * static_cast<std::uint64_t>(h.height) : 0;
    if (pixels == 0 || n != pixels) return fail("sample count doesn't match the frame size");
    if (rawBytes != static_cast<std::uint64_t>(n) * 2)
        return fail("byte size doesn't match the sample count");
    if (!decodableSize(rawBytes)) return fail("byte size is too large to decode");
    if (!decodedSizeFits(rawBytes, compSize, codec))
        return fail("compressed size doesn't fit the byte size");
    scratch.resize(compSize);
    if (!in.read(reinterpret_cast<char*>(scratch.data()), compSize)) return fail("block is cut off");
    if (hilo) {
        std::vector<std::uint8_t> planes(rawBytes);
        if (!decodedExactly(decompress(scratch.data(), compSize, planes.data(), planes.size(),
                                       Codec::LZ4), rawBytes))
            return fail("data didn't decode to the byte size");
        dst.depth.resize(n);
        for (std::uint32_t i=0;i<n;++i)
            dst.depth[i]=static_cast<std::uint16_t>((static_cast<std::uint16_t>(planes[i])<<8)|planes[n+i]);
    } else {
        dst.depth.resize(n);
        if (!decodedExactly(decompress(scratch.data(), compSize, dst.depth.data(),
                                       dst.depth.size() * sizeof(std::uint16_t), codec), rawBytes))
            return fail("data didn't decode to the byte size");
    }
    return true;
}

inline bool parseColorPayload(std::istream& in, const TcdcHeader& h, std::uint32_t len,
                              std::uint64_t room, DepthFrame& dst,
                              std::vector<std::uint8_t>& scratch,
                              std::uint64_t& used, const char*& why) {
    auto fail = [&](const char* reason) {
        if (dst.color.isAllocated()) dst.color = Pixels{};
        why = reason;
        return false;
    };
    used = 0;
    std::int32_t cw=0, ch=0; std::uint8_t chn=0; std::uint32_t rawBytes=0, compSize=0;
    if (len < LEGACY_COLOR_BLOCK_FIELDS || room < COLOR_BLOCK_FIELDS)
        return fail("block is shorter than its size fields");
    if (!rd(in,cw) || !rd(in,ch) || !rd(in,chn) || !rd(in,rawBytes) || !rd(in,compSize))
        return fail("block is cut off");
    // The length counts all 17 bytes of fields, or exactly 13 + compressed size
    // in a file written before the color length was fixed.
    const std::uint64_t payloadBytes = COLOR_BLOCK_FIELDS + static_cast<std::uint64_t>(compSize);
    const bool legacyLen = len == LEGACY_COLOR_BLOCK_FIELDS + static_cast<std::uint64_t>(compSize);
    if ((!legacyLen && payloadBytes > len) || payloadBytes > room)
        return fail("compressed size runs past the block");
    used = payloadBytes;
    const Codec codec=(h.colorCodec==static_cast<std::uint8_t>(ColorCodecId::LZ4))?Codec::LZ4:Codec::None;
    if (cw <= 0 || ch <= 0) return fail("width and height must be positive");
    if (chn != 1 && chn != 3 && chn != 4) return fail("channel count must be 1, 3 or 4");
    // cw, ch < 2^31 and chn <= 4, so the 64-bit product can't wrap.
    if (rawBytes != static_cast<std::uint64_t>(cw) * static_cast<std::uint64_t>(ch) * chn)
        return fail("byte size doesn't match width x height x channels");
    if (!decodableSize(rawBytes)) return fail("byte size is too large to decode");
    if (!decodedSizeFits(rawBytes, compSize, codec))
        return fail("compressed size doesn't fit the byte size");
    scratch.resize(compSize);
    if (!in.read(reinterpret_cast<char*>(scratch.data()), compSize)) return fail("block is cut off");
    if (!dst.color.isAllocated() || dst.color.getWidth()!=cw ||
        dst.color.getHeight()!=ch || dst.color.getChannels()!=chn) {
        dst.color.allocate(cw, ch, chn);
    }
    if (!decodedExactly(decompress(scratch.data(), compSize, dst.color.getData(),
                                   dst.color.getTotalBytes(), codec), rawBytes))
        return fail("data didn't decode to the byte size");
    return true;
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
