// =============================================================================
// tcxDepthRecord tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR via examples/build_all.py
// --addon-tests-only (exit 0 = pass, non-zero = fail). Console only.
//
//   1. Round trip: recordings written by DepthRecorder (every depth / color
//      codec, 1/3/4-channel color, depth only, a camera-sized frame, a custom
//      block per frame, and frames that are smooth, incompressible and all
//      zero) play back byte-for-byte, with no warnings. Each block's length
//      is its payload size, so walking a frame by type and length alone
//      visits every block and ends exactly at the next frame. A copy with
//      every color block length written the old way (13 + compressed size)
//      plays the same.
//   2. Block sizes: copies of a valid recording with one field of the middle
//      frame changed (depth sample count / byte size / compressed size, color
//      width / height / channels / byte size / compressed size, a block
//      length, an index offset, data that doesn't decode or decodes short,
//      a second depth / color block that is skipped, the stream manifest
//      count, the header magic) are skipped or refused with the expected
//      message, and the other frames still play.
//      Depth / color blocks with trailing extension bytes still allow the
//      following blocks to play, even when the known payload is skipped.
//   3. Header frame size: 0x0 and sizes up to width x height x 4 = INT_MAX
//      open; negative sizes and larger ones are refused.
//   4. The parsers refuse a byte size above INT_MAX before allocating.
//   5. Reopening one object warns about a skipped block again, and a failed
//      reopen leaves the manifest empty. A refused open (wrong magic, manifest
//      count, header frame size) doesn't keep the file open, and the same
//      object then opens a valid file; setup() twice opens again.
//   6. depthToImage() draws nothing for a depth plane shorter than w*h.
//   7. depthToImage() / colorToImage() / irToImage() give the expected pixels
//      for normal frames, into a new Image, one of another size and one of the
//      same size.
//
// Pass a name fragment as the first argument to run only the matching cases.
// =============================================================================

#include <tcxDepthRecord.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace tc;
using namespace tcx::depthrecord;
using tcx::depthcamera::DepthCamera;
using tcx::depthcamera::DepthFrame;
using tcx::depthcamera::StreamFreshness;

static int g_pass = 0, g_fail = 0;
static void check(const string& name, bool ok) {
    printf("%-64s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);  // flush each line so CI logs survive a later crash
    ok ? ++g_pass : ++g_fail;
}

static string g_only;  // run only the cases whose name contains this
static bool selected(const string& name) {
    return g_only.empty() || name.find(g_only) != string::npos;
}

// -----------------------------------------------------------------------------
// Frame content
// -----------------------------------------------------------------------------

struct Dims {
    int w = 64, h = 48;             // depth
    int cw = 80, ch = 60, chn = 4;  // color (chn 0 = no color)
};

constexpr int FRAMES = 3;
constexpr uint8_t CUSTOM_TYPE = BLOCK_CUSTOM_BASE + 1;

// Frame k: 0 = smooth gradient, 1 = pseudo-random (does not compress),
// 2 = all zero (compresses as far as LZ4 goes).
static void fillFrame(DepthFrame& f, const Dims& d, int k) {
    f.w = d.w; f.h = d.h; f.depthScale = 0.001f;
    f.intrinsics.width = d.w; f.intrinsics.height = d.h;
    f.intrinsics.fx = f.intrinsics.fy = static_cast<float>(d.w);
    f.intrinsics.cx = d.w * 0.5f; f.intrinsics.cy = d.h * 0.5f;
    f.timestamp = 1.0 + 0.25 * k;

    uint32_t seed = 12345u + static_cast<uint32_t>(k);
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };

    f.depth.assign(static_cast<size_t>(d.w) * d.h, 0);
    for (size_t i = 0; i < f.depth.size(); ++i) {
        f.depth[i] = k == 0 ? static_cast<uint16_t>(500 + i % 3000)
                   : k == 1 ? static_cast<uint16_t>(rnd())
                   : 0;
    }
    if (d.chn > 0) {
        f.color.allocate(d.cw, d.ch, d.chn);
        unsigned char* c = f.color.getData();
        const size_t bytes = static_cast<size_t>(d.cw) * d.ch * d.chn;
        for (size_t i = 0; i < bytes; ++i) {
            c[i] = k == 0 ? static_cast<unsigned char>(i * 7)
                 : k == 1 ? static_cast<unsigned char>(rnd())
                 : 0;
        }
    } else if (f.color.isAllocated()) {
        f.color = Pixels{};
    }
}

static vector<uint8_t> customPayload(int k) {
    vector<uint8_t> v(16 + k);
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<uint8_t>(k * 31 + i);
    return v;
}

// A camera that serves fillFrame() frames 0, 1, 2, ...
class PatternCamera : public DepthCamera {
public:
    explicit PatternCamera(const Dims& d) : d_(d) {}

protected:
    bool openDevice() override { k_ = 0; return true; }
    void closeDevice() override {}
    StreamFreshness captureInto(DepthFrame& dst) override {
        fillFrame(dst, d_, k_++);
        StreamFreshness s;
        s.depth = true;
        s.color = d_.chn > 0;
        return s;
    }

private:
    Dims d_;
    int k_ = 0;
};

// Appends one custom block per frame, so every file also carries a block type
// the official player does not decode.
class TaggedRecorder : public DepthRecorder {
protected:
    void writeExtraBlocks(const DepthCamera&) override {
        const vector<uint8_t> p = customPayload(k_++);
        writeBlock(CUSTOM_TYPE, p.data(), static_cast<uint32_t>(p.size()));
    }

private:
    int k_ = 0;
};

// Playback that collects the custom blocks it is handed.
class ProbePlayback : public PlaybackDepthCamera {
public:
    using PlaybackDepthCamera::PlaybackDepthCamera;
    vector<vector<uint8_t>> extra;

protected:
    bool readExtraBlock(uint8_t type, const uint8_t* data, uint32_t len, double) override {
        if (type != CUSTOM_TYPE) return false;
        extra.emplace_back(data, data + len);
        return true;
    }
    bool decodesBlockType(uint8_t type) const override { return type == CUSTOM_TYPE; }
};

// -----------------------------------------------------------------------------
// Record / play
// -----------------------------------------------------------------------------

static bool writeRecording(const filesystem::path& path, const Dims& d,
                           DepthCodecId dc, ColorCodecId cc) {
    PatternCamera cam(d);
    cam.enableDepth();
    cam.enableColor();
    if (!cam.setup()) return false;
    TaggedRecorder rec;
    if (!rec.start(path.string(), REC_ALL, dc, cc)) return false;
    for (int k = 0; k < FRAMES; ++k) {
        cam.update();
        rec.record(cam);
    }
    rec.stop();
    cam.close();
    return true;
}

// Collects warnings and errors logged while it lives.
struct LogCapture {
    vector<string> lines;
    EventListener sub;
    LogCapture()
        : sub(getLogger().onLog.listen([this](LogEventArgs& e) {
              if (e.level >= LogLevel::Warning) lines.push_back(e.message);
          })) {}
};

struct PlayedFrame {
    bool depthNew = false, colorNew = false;
    bool extraSeen = false;
    vector<uint8_t> extra;
    DepthFrame f;
};

struct Played {
    bool opened = false;
    vector<uint8_t> blockTypes;
    vector<PlayedFrame> frames;
    vector<string> log;
};

static Played play(const filesystem::path& path) {
    Played r;
    LogCapture cap;
    auto p = make_shared<ProbePlayback>(path.string());
    p->enableDepth();
    p->enableColor();
    p->setLoop(false);
    r.opened = p->setup();
    r.blockTypes = p->getBlockTypes();
    if (r.opened) {
        for (int k = 0; k < FRAMES; ++k) {
            const size_t before = p->extra.size();
            p->update();
            PlayedFrame pf;
            pf.depthNew = p->isFrameNew();
            pf.colorNew = p->isColorFrameNew();
            pf.extraSeen = p->extra.size() > before;
            if (pf.extraSeen) pf.extra = p->extra.back();
            const DepthFrame& f = p->currentFrame();
            pf.f.w = f.w; pf.f.h = f.h; pf.f.timestamp = f.timestamp;
            pf.f.depth = f.depth;
            if (f.color.isAllocated()) {
                pf.f.color.allocate(f.color.getWidth(), f.color.getHeight(),
                                    f.color.getChannels());
                memcpy(pf.f.color.getData(), f.color.getData(), f.color.getTotalBytes());
            }
            r.frames.push_back(std::move(pf));
        }
        p->close();
    }
    r.log = cap.lines;
    return r;
}

static bool sameDepth(const DepthFrame& got, const DepthFrame& want) {
    return got.w == want.w && got.h == want.h && got.depth == want.depth;
}

static bool sameColor(const DepthFrame& got, const DepthFrame& want) {
    if (got.color.isAllocated() != want.color.isAllocated()) return false;
    if (!want.color.isAllocated()) return true;
    return got.color.getWidth() == want.color.getWidth() &&
           got.color.getHeight() == want.color.getHeight() &&
           got.color.getChannels() == want.color.getChannels() &&
           memcmp(got.color.getData(), want.color.getData(), want.color.getTotalBytes()) == 0;
}

// Frame k played back exactly as recorded.
static bool frameIntact(const PlayedFrame& pf, const Dims& d, int k) {
    DepthFrame want;
    fillFrame(want, d, k);
    return pf.depthNew && pf.colorNew == (d.chn > 0) &&
           pf.f.timestamp == want.timestamp &&
           sameDepth(pf.f, want) && sameColor(pf.f, want) &&
           pf.extraSeen && pf.extra == customPayload(k);
}

// -----------------------------------------------------------------------------
// File layout (to find the size fields of a valid recording)
// -----------------------------------------------------------------------------

static uint32_t get32(const vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    memcpy(&v, b.data() + at, 4);
    return v;
}
static void put32(vector<uint8_t>& b, size_t at, uint32_t v) { memcpy(b.data() + at, &v, 4); }

struct BlockAt {
    uint8_t type = 0;
    size_t at = 0;     // offset of the type byte; length at +1, payload at +5
    uint32_t len = 0;
    size_t payload() const { return at + 5; }
};

// Field offsets inside a depth / color payload.
constexpr size_t D_N = 0, D_RAW = 4, D_COMP = 8, D_DATA = 12;
constexpr size_t C_W = 0, C_H = 4, C_CHN = 8, C_RAW = 9, C_COMP = 13, C_DATA = 17;

// Frame start offsets from the index, and where the last frame ends.
static vector<uint64_t> frameOffsets(const vector<uint8_t>& b, uint64_t& indexOffset) {
    TcdcHeader h;
    memcpy(&h, b.data(), sizeof(h));
    vector<uint64_t> offsets;
    for (uint32_t i = 0; i < h.frameCount; ++i) {
        uint64_t off = 0;
        memcpy(&off, b.data() + h.indexOffset + i * 16 + 8, 8);
        offsets.push_back(off);
    }
    indexOffset = h.indexOffset;
    return offsets;
}

// Blocks of each frame, read from a valid file. Depth and color payloads end
// where their own size fields say, as the official player reads them (a file
// written before the color length was fixed states it as 13 + compressed size,
// though the fields take 17 bytes).
static vector<vector<BlockAt>> layoutOf(const vector<uint8_t>& b) {
    uint64_t indexOffset = 0;
    const vector<uint64_t> offsets = frameOffsets(b, indexOffset);
    vector<vector<BlockAt>> frames;
    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint64_t end = i + 1 < offsets.size() ? offsets[i + 1] : indexOffset;
        vector<BlockAt> blocks;
        size_t at = static_cast<size_t>(offsets[i]) + 8;  // after the timestamp
        while (at < end) {
            BlockAt blk;
            blk.type = b[at];
            blk.at = at;
            blk.len = get32(b, at + 1);
            blocks.push_back(blk);
            at += 5 + (blk.type == BLOCK_DEPTH ? D_DATA + get32(b, blk.payload() + D_COMP)
                     : blk.type == BLOCK_COLOR ? C_DATA + get32(b, blk.payload() + C_COMP)
                     : blk.len);
        }
        if (at != end) {
            printf("layout: frame %zu does not end at the next frame\n", i);
            exit(1);
        }
        frames.push_back(blocks);
    }
    return frames;
}

// Walks every frame the way a reader that skips blocks by length does: using
// only each block's type and length. Every block must end within its frame, the
// last one exactly at the next frame, the frame must hold `types` in order, and
// a depth / color block's length must equal its fields plus compressed size.
static bool walksByLength(const vector<uint8_t>& b, const vector<uint8_t>& types) {
    uint64_t indexOffset = 0;
    const vector<uint64_t> offsets = frameOffsets(b, indexOffset);
    if (offsets.size() != FRAMES) return false;
    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint64_t end = i + 1 < offsets.size() ? offsets[i + 1] : indexOffset;
        uint64_t at = offsets[i] + 8;  // after the timestamp
        vector<uint8_t> seen;
        while (at < end) {
            if (at + 5 > end) {
                printf("  frame %zu: block header at %llu runs past the frame\n", i,
                       static_cast<unsigned long long>(at));
                return false;
            }
            const uint8_t type = b[at];
            const uint32_t len = get32(b, at + 1);
            const size_t payload = static_cast<size_t>(at) + 5;
            if (type == BLOCK_DEPTH || type == BLOCK_COLOR) {
                if (payload + (type == BLOCK_DEPTH ? D_DATA : C_DATA) > end) {
                    printf("  frame %zu: block type %d has no room for its fields\n", i, type);
                    return false;
                }
                const uint64_t want = type == BLOCK_DEPTH
                    ? D_DATA + uint64_t(get32(b, payload + D_COMP))
                    : C_DATA + uint64_t(get32(b, payload + C_COMP));
                if (len != want) {
                    printf("  frame %zu: block type %d has length %u, payload %llu\n", i, type,
                           len, static_cast<unsigned long long>(want));
                    return false;
                }
            }
            seen.push_back(type);
            at = payload + uint64_t(len);
        }
        if (at != end || seen != types) {
            printf("  frame %zu: walk by length ends at %llu, frame ends at %llu, %zu blocks\n",
                   i, static_cast<unsigned long long>(at), static_cast<unsigned long long>(end),
                   seen.size());
            return false;
        }
    }
    return true;
}

static const BlockAt& blockOf(const vector<vector<BlockAt>>& layout, int frame, uint8_t type) {
    for (const BlockAt& blk : layout[frame]) {
        if (blk.type == type) return blk;
    }
    printf("layout: no block of type %d in frame %d\n", type, frame);
    exit(1);
}

static vector<uint8_t> readAll(const filesystem::path& p) {
    ifstream in(p, ios::binary);
    return vector<uint8_t>(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
}
static void writeAll(const filesystem::path& p, const vector<uint8_t>& b) {
    ofstream out(p, ios::binary | ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<streamsize>(b.size()));
}

// -----------------------------------------------------------------------------
// Cases with one size field changed
// -----------------------------------------------------------------------------

constexpr uint8_t FILLER_TYPE = BLOCK_CUSTOM_BASE + 2;  // ProbePlayback ignores it

// Add four unknown trailing bytes to a known block in frame 1. Its compressed
// data stays the same; the TLV length, index location and later frame offsets
// account for the inserted bytes, leaving the following blocks intact.
static void addBlockExtension(uint8_t type, vector<uint8_t>& b,
                              const vector<vector<BlockAt>>& L) {
    const BlockAt& k = blockOf(L, 1, type);
    const size_t tail = k.payload() + k.len;
    uint64_t indexOffset = 0;
    vector<uint64_t> offsets = frameOffsets(b, indexOffset);
    b.insert(b.begin() + tail, 4, 0xFF);
    put32(b, k.at + 1, k.len + 4);
    indexOffset += 4;
    memcpy(b.data() + offsetof(TcdcHeader, indexOffset), &indexOffset, 8);
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (offsets[i] > tail) offsets[i] += 4;
        memcpy(b.data() + indexOffset + i * 16 + 8, &offsets[i], 8);
    }
}

// Replaces the depth data of frame 1 with a valid LZ4 stream that decodes to 2
// bytes less than the byte size, and keeps the sizes consistent with it. The
// bytes it frees become a filler block, so the blocks after it stay in place.
static void depthDataShort(vector<uint8_t>& b, const vector<vector<BlockAt>>& L) {
    const BlockAt& k = blockOf(L, 1, BLOCK_DEPTH);
    const uint32_t oldComp = get32(b, k.payload() + D_COMP);
    const vector<uint8_t> shorter(get32(b, k.payload() + D_RAW) - 2, 0);
    vector<uint8_t> comp;
    if (!compress(shorter.data(), shorter.size(), comp, Codec::LZ4) ||
        comp.size() + 5 > oldComp) {
        printf("depthDataShort: no room for the filler block\n");
        exit(1);
    }
    const uint32_t newComp = static_cast<uint32_t>(comp.size());
    put32(b, k.at + 1, static_cast<uint32_t>(D_DATA) + newComp);
    put32(b, k.payload() + D_COMP, newComp);
    memcpy(b.data() + k.payload() + D_DATA, comp.data(), comp.size());
    const size_t filler = k.payload() + D_DATA + newComp;
    b[filler] = FILLER_TYPE;
    put32(b, filler + 1, oldComp - newComp - 5);
}

// Replaces frame 1's custom block with a second block of `type` whose sizes
// match the frame but whose compressed size is 0, so it can't decode; a filler
// block takes the bytes left over. The custom block is the last one in the
// frame, so nothing after it moves.
static void secondBlockWithoutData(uint8_t type, vector<uint8_t>& b,
                                   const vector<vector<BlockAt>>& L) {
    const BlockAt& e = blockOf(L, 1, CUSTOM_TYPE);
    const BlockAt& k = blockOf(L, 1, type);
    const size_t fields = type == BLOCK_DEPTH ? D_DATA : C_DATA;
    const size_t total = 5 + e.len;
    if (total < 5 + fields || (total - 5 - fields != 0 && total - 5 - fields < 5)) {
        printf("secondBlockWithoutData: the custom block has the wrong size\n");
        exit(1);
    }
    memcpy(b.data() + e.at, b.data() + k.at, 5 + fields);
    // The length DepthRecorder writes for a block with no compressed data.
    put32(b, e.at + 1, static_cast<uint32_t>(fields));
    put32(b, e.payload() + (type == BLOCK_DEPTH ? D_COMP : C_COMP), 0);
    const size_t filler = e.at + 5 + fields;
    if (filler < e.at + total) {
        b[filler] = FILLER_TYPE;
        put32(b, filler + 1, static_cast<uint32_t>(e.at + total - filler - 5));
    }
}

// What frame 1 (the changed one) should give for each stream.
enum class S {
    Read,     // fresh and equal to what was recorded (custom block: handed over intact)
    Skipped,  // not fresh (custom block: not handed over)
    Any,      // not checked: blocks after a skipped one may be lost
};

struct Mutation {
    const char* name;
    DepthCodecId dc;
    ColorCodecId cc;
    S depth, color, extra;
    const char* message;  // expected in the one warning / error; nullptr = nothing logged
    function<void(vector<uint8_t>&, const vector<vector<BlockAt>>&)> apply;
    bool refused = false;   // setup() fails
    bool lastLost = false;  // frame 2 is not played (its index entry is changed)
};

static bool streamAs(S want, bool fresh, bool emptyWhenFresh, bool intact, bool otherFresh) {
    switch (want) {
        case S::Read:    return fresh && intact;
        case S::Skipped: return !fresh && (!otherFresh || emptyWhenFresh);
        case S::Any:     return true;
    }
    return false;
}

static void runMutation(const Mutation& m, const filesystem::path& dir, const Dims& d) {
    const string base = string("tcdcCorruptSizes/") + m.name;
    if (!selected(base)) return;

    const filesystem::path valid = dir / (string(m.name) + "-valid.tcdc");
    const filesystem::path changed = dir / (string(m.name) + ".tcdc");
    if (!writeRecording(valid, d, m.dc, m.cc)) {
        check(base + ": write source recording", false);
        return;
    }
    vector<uint8_t> bytes = readAll(valid);
    m.apply(bytes, layoutOf(bytes));
    writeAll(changed, bytes);

    const Played r = play(changed);
    const bool logOk = m.message
        ? r.log.size() == 1 && r.log[0].find(m.message) != string::npos
        : r.log.empty();
    if (!logOk) {
        for (const string& line : r.log) printf("  logged: %s\n", line.c_str());
        if (m.message) printf("  expected one message containing: %s\n", m.message);
    }

    if (m.refused) {
        check(base + ": open refused", !r.opened);
        check(base + ": manifest empty after a refused open", r.blockTypes.empty());
        check(base + ": reported", logOk);
        return;
    }

    bool ok = r.opened && r.frames.size() == FRAMES && frameIntact(r.frames[0], d, 0);
    ok = ok && (m.lastLost ? !r.frames[2].depthNew && !r.frames[2].colorNew
                           : frameIntact(r.frames[2], d, 2));
    check(base + ": other frames intact", ok);
    if (!ok) return;

    DepthFrame want;
    fillFrame(want, d, 1);
    const PlayedFrame& f1 = r.frames[1];
    const bool depthOk = streamAs(m.depth, f1.depthNew, f1.f.depth.empty(),
                                  sameDepth(f1.f, want), f1.colorNew);
    const bool colorOk = streamAs(m.color, f1.colorNew, !f1.f.color.isAllocated(),
                                  sameColor(f1.f, want), f1.depthNew);
    const bool extraOk = m.extra == S::Any ||
                         (m.extra == S::Read ? f1.extraSeen && f1.extra == customPayload(1)
                                             : !f1.extraSeen);
    check(base + ": frame 1 as expected", depthOk && colorOk && extraOk);
    check(base + (m.message ? ": reported once" : ": nothing logged"), logOk);
}

static vector<Mutation> mutations() {
    using D = DepthCodecId;
    using C = ColorCodecId;
    const D hilo = D::HiloLZ4;
    const C lz4 = C::LZ4;
    const S R = S::Read, X = S::Skipped, A = S::Any;
    return {
        // --- depth block ---
        {"depthTrailingExtension", hilo, lz4, R, R, R, nullptr,
         [](auto& b, auto& L) { addBlockExtension(BLOCK_DEPTH, b, L); }},
        {"depthSkippedWithTrailingExtension", hilo, lz4, X, R, R, "byte size doesn't match the sample count",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_RAW) + 2);
                                addBlockExtension(BLOCK_DEPTH, b, L); }},
        {"depthRawBytesLarger", hilo, lz4, X, R, R, "byte size doesn't match the sample count",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_RAW) + 2); }},
        {"depthRawBytesSmaller", hilo, lz4, X, R, R, "byte size doesn't match the sample count",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_N)); }},
        {"depthRawBytesLargerLz4", D::LZ4, lz4, X, R, R, "byte size doesn't match the sample count",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_RAW) + 2); }},
        // Byte size, compressed size and length all 64 larger: only the byte
        // size gives it away.
        {"depthRawBytesLargerUncompressed", D::Raw, C::Raw, X, A, A, "byte size doesn't match the sample count",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.at + 1, k.len + 64);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_RAW) + 64);
                                put32(b, k.payload() + D_COMP, get32(b, k.payload() + D_COMP) + 64); }},
        {"depthCountLarger", hilo, lz4, X, R, R, "sample count doesn't match the frame size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_N, get32(b, k.payload() + D_N) + 1);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_RAW) + 2); }},
        {"depthCountSmaller", D::LZ4, lz4, X, R, R, "sample count doesn't match the frame size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_N, get32(b, k.payload() + D_N) - 1);
                                put32(b, k.payload() + D_RAW, get32(b, k.payload() + D_RAW) - 2); }},
        {"depthCompSizePastBlock", hilo, lz4, X, X, X, "compressed size runs past the block",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_COMP, k.len - 12 + 1); }},
        {"depthCompSizePastFile", hilo, lz4, X, X, X, "compressed size runs past the block",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_COMP, 0xFFFFFFF0u); }},
        {"depthCompSizeUncompressedMismatch", D::Raw, lz4, X, A, A, "compressed size doesn't fit the byte size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_COMP, get32(b, k.payload() + D_COMP) - 2); }},
        {"depthCompSizeTooSmall", hilo, lz4, X, A, A, "compressed size doesn't fit the byte size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.payload() + D_COMP, 1); }},
        {"depthDataDoesNotDecode", hilo, lz4, X, R, R, "didn't decode to the byte size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                const size_t n = get32(b, k.payload() + D_COMP);
                                memset(b.data() + k.payload() + D_DATA, 0xFF, n); }},
        {"depthDataDoesNotDecodeLz4", D::LZ4, lz4, X, R, R, "didn't decode to the byte size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                const size_t n = get32(b, k.payload() + D_COMP);
                                memset(b.data() + k.payload() + D_DATA, 0xFF, n); }},
        // A valid LZ4 stream for a plane 2 bytes short, all sizes consistent.
        {"depthDataDecodesShort", D::LZ4, lz4, X, R, R, "didn't decode to the byte size",
         depthDataShort},
        {"depthDataDecodesShortHilo", hilo, lz4, X, R, R, "didn't decode to the byte size",
         depthDataShort},
        {"depthLenBelowFields", hilo, lz4, X, X, X, "shorter than its size fields",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.at + 1, 4); }},
        {"depthLenPastFrame", hilo, lz4, X, X, X, "runs past the end of its frame",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.at + 1, 0xFFFFFFF0u); }},
        // A valid depth block, then one that can't decode: the stream ends up
        // empty, so it isn't new.
        {"depthSecondBlockSkipped", hilo, lz4, X, R, X, "compressed size doesn't fit the byte size",
         [](auto& b, auto& L) { secondBlockWithoutData(BLOCK_DEPTH, b, L); }},

        // --- color block ---
        {"colorTrailingExtension", hilo, lz4, R, R, R, nullptr,
         [](auto& b, auto& L) { addBlockExtension(BLOCK_COLOR, b, L); }},
        {"colorSkippedWithTrailingExtension", hilo, lz4, R, X, R, "width and height must be positive",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_W, static_cast<uint32_t>(-80));
                                addBlockExtension(BLOCK_COLOR, b, L); }},
        // The length files written before the color length was fixed state
        // (13 + compressed size) plays like the one DepthRecorder writes now.
        {"colorLenBeforeFix", hilo, lz4, R, R, R, nullptr,
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.at + 1, k.len - 4); }},
        // A length between the two (15 + compressed size) is neither: the
        // payload runs 2 bytes past the block.
        {"colorLenBetweenForms", hilo, lz4, R, X, X, "compressed size runs past the block",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.at + 1, k.len - 2); }},
        {"colorWidthNegative", hilo, lz4, R, X, R, "width and height must be positive",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_W, static_cast<uint32_t>(-80)); }},
        {"colorHeightZero", hilo, lz4, R, X, R, "width and height must be positive",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_H, 0); }},
        {"colorChannelsTwo", hilo, lz4, R, X, R, "channel count must be 1, 3 or 4",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                b[k.payload() + C_CHN] = 2;
                                put32(b, k.payload() + C_RAW, 80 * 60 * 2); }},
        {"colorRawBytesLarger", hilo, lz4, R, X, R, "byte size doesn't match width x height x channels",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_RAW, get32(b, k.payload() + C_RAW) + 4); }},
        // Byte size, compressed size and length all 16 larger (the custom block
        // after it leaves room): only the byte size gives it away.
        {"colorRawBytesLargerUncompressed", hilo, C::Raw, R, X, A, "byte size doesn't match width x height x channels",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.at + 1, k.len + 16);
                                put32(b, k.payload() + C_RAW, get32(b, k.payload() + C_RAW) + 16);
                                put32(b, k.payload() + C_COMP, get32(b, k.payload() + C_COMP) + 16); }},
        // width x height x channels is 2^32 + the stored byte size: it only
        // matches if the product is computed in 32 bits.
        {"colorSizeBeyond32Bits", hilo, lz4, R, X, R, "byte size doesn't match width x height x channels",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_W, (1u << 30) + get32(b, k.payload() + C_RAW) / 4);
                                put32(b, k.payload() + C_H, 1); }},
        {"colorSizeHuge", hilo, lz4, R, X, R, "byte size doesn't match width x height x channels",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_W, 0x7FFFFFFFu);
                                put32(b, k.payload() + C_H, 0x7FFFFFFFu); }},
        {"colorSizeBeyondData", hilo, lz4, R, X, R, "compressed size doesn't fit the byte size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_W, 8192);
                                put32(b, k.payload() + C_H, 8192);
                                put32(b, k.payload() + C_RAW, 8192u * 8192u * 4u); }},
        {"colorDataDoesNotDecode", hilo, lz4, R, X, R, "didn't decode to the byte size",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                const size_t n = get32(b, k.payload() + C_COMP);
                                memset(b.data() + k.payload() + C_DATA, 0xFF, n); }},
        {"colorCompSizePastBlock", hilo, lz4, R, X, X, "compressed size runs past the block",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_COMP, k.len - 17 + 1); }},
        {"colorCompSizePastFile", hilo, lz4, R, X, X, "compressed size runs past the block",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.payload() + C_COMP, 0xFFFFFFF0u); }},
        // Length reaching exactly to the end of the frame, compressed size
        // counted with 13 bytes of fields: the payload's 17 bytes of fields
        // plus the compressed data end 4 bytes past the frame.
        {"colorPayloadPastFrame", hilo, lz4, R, X, X, "compressed size runs past the block",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                const auto& e = blockOf(L, 1, CUSTOM_TYPE);
                                const uint32_t room = static_cast<uint32_t>(e.payload() + e.len - k.payload());
                                put32(b, k.at + 1, room);
                                put32(b, k.payload() + C_COMP, room - 13); }},
        {"colorLenBelowFields", hilo, lz4, R, X, X, "shorter than its size fields",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.at + 1, 12); }},
        {"colorSecondBlockSkipped", hilo, lz4, R, X, X, "compressed size doesn't fit the byte size",
         [](auto& b, auto& L) { secondBlockWithoutData(BLOCK_COLOR, b, L); }},

        // --- custom block ---
        {"customLenPastFrame", hilo, lz4, R, R, X, "runs past the end of its frame",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, CUSTOM_TYPE);
                                put32(b, k.at + 1, k.len + 4096); }},
        {"customLenPastFile", hilo, lz4, R, R, X, "runs past the end of its frame",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, CUSTOM_TYPE);
                                put32(b, k.at + 1, static_cast<uint32_t>(b.size())); }},
        {"customLenHuge", hilo, lz4, R, R, X, "runs past the end of its frame",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, CUSTOM_TYPE);
                                put32(b, k.at + 1, 0xFFFFFFFFu); }},

        // The index sends frame 1 up to far past the end of the file: its
        // blocks are still bounded by the file.
        {"indexPastFile", hilo, lz4, R, R, X, "runs past the end of its frame",
         [](auto& b, auto& L) { TcdcHeader h;
                                memcpy(&h, b.data(), sizeof(h));
                                const uint64_t pastEnd = uint64_t(1) << 40;
                                memcpy(b.data() + h.indexOffset + 2 * 16 + 8, &pastEnd, 8);
                                const auto& k = blockOf(L, 1, CUSTOM_TYPE);
                                put32(b, k.at + 1, 0xFFFFFFFFu); },
         false, true},

        // --- header ---
        {"manifestCountOverLimit", hilo, lz4, A, A, A, "stream manifest",
         [](auto& b, auto&) { b[offsetof(TcdcHeader, streamTypeCount)] = 200; }, true},
        {"headerMagicWrong", hilo, lz4, A, A, A, "not a .tcdc file",
         [](auto& b, auto&) { b[0] = 'X'; }, true},
    };
}

// -----------------------------------------------------------------------------
// Header frame size
// -----------------------------------------------------------------------------

// A valid recording with the header's width and height replaced. Sizes that
// open but don't match the depth blocks play color only.
static void runHeaderSize(const filesystem::path& dir) {
    struct Case { const char* name; int32_t w, h; bool opens; };
    const Case cases[] = {
        {"recorded", 64, 48, true},
        {"zero", 0, 0, true},
        {"atLimit", 536870911, 1, true},             // x 4 = INT_MAX - 3
        {"pastLimit", 536870912, 1, false},          // x 4 = INT_MAX + 1
        {"productWrapsTo0", 65536, 65536, false},    // x 4 = 2^34, 0 in 32 bits
        {"widthNegative", -64, 48, false},
        {"bothNegative", -64, -48, false},           // positive product
    };
    const Dims d;
    for (const Case& c : cases) {
        const string base = string("headerSize/") + c.name;
        if (!selected(base)) continue;
        const filesystem::path path = dir / (string("headerSize-") + c.name + ".tcdc");
        if (!writeRecording(path, d, DepthCodecId::HiloLZ4, ColorCodecId::LZ4)) {
            check(base + ": write source recording", false);
            continue;
        }
        vector<uint8_t> bytes = readAll(path);
        put32(bytes, offsetof(TcdcHeader, width), static_cast<uint32_t>(c.w));
        put32(bytes, offsetof(TcdcHeader, height), static_cast<uint32_t>(c.h));
        writeAll(path, bytes);

        const Played r = play(path);
        auto loggedOnce = [&r](const char* message) {
            return r.log.size() == 1 && r.log[0].find(message) != string::npos;
        };
        auto showLog = [&r] {
            for (const string& line : r.log) printf("  logged: %s\n", line.c_str());
        };
        if (!c.opens) {
            check(base + ": open refused", !r.opened && r.blockTypes.empty());
            const bool reported = loggedOnce("is out of range");
            check(base + ": reported", reported);
            if (!reported) showLog();
            continue;
        }
        bool ok = r.opened && r.frames.size() == FRAMES;
        const bool matches = c.w == d.w && c.h == d.h;
        for (int k = 0; ok && k < FRAMES; ++k) {
            if (matches) {
                ok = frameIntact(r.frames[k], d, k);
            } else {
                DepthFrame want;
                fillFrame(want, d, k);
                const PlayedFrame& pf = r.frames[k];
                ok = !pf.depthNew && pf.f.depth.empty() && pf.colorNew && sameColor(pf.f, want);
            }
        }
        check(base + ": opens and plays", ok);
        const bool logOk = matches ? r.log.empty()
                                   : loggedOnce("sample count doesn't match the frame size");
        check(base + (matches ? ": nothing logged" : ": depth skip reported once"), logOk);
        if (!logOk) showLog();
    }
}

// -----------------------------------------------------------------------------
// Byte size above INT_MAX
// -----------------------------------------------------------------------------

// Size fields that agree with each other, with a compressed size that could
// hold them, but a byte size decompress() can't produce: refused before the
// parser allocates anything. Only the fields are in the stream.
static void runByteSizeAboveIntMax() {
    const string base = "byteSizeAboveIntMax";
    auto fieldStream = [](initializer_list<pair<const void*, size_t>> fields) {
        string s;
        for (const auto& f : fields) s.append(static_cast<const char*>(f.first), f.second);
        return istringstream(s);
    };
    if (selected(base + "/depth")) {
        TcdcHeader h{};
        h.width = 32768; h.height = 32768;
        h.depthCodec = static_cast<uint8_t>(DepthCodecId::HiloLZ4);
        const uint32_t n = 1u << 30, raw = 1u << 31, comp = raw / 255 + 1;
        istringstream in = fieldStream({{&n, 4}, {&raw, 4}, {&comp, 4}});
        DepthFrame dst;
        vector<uint8_t> scratch;
        uint64_t used = 0;
        const char* why = nullptr;
        const bool ok = tcd_detail::parseDepthPayload(in, h, 12 + comp, 12 + comp, dst,
                                                      scratch, used, why);
        check(base + "/depth: refused before allocating",
              !ok && why && string(why).find("too large to decode") != string::npos &&
              scratch.capacity() == 0 && dst.depth.capacity() == 0);
    }
    if (selected(base + "/color")) {
        TcdcHeader h{};
        h.colorCodec = static_cast<uint8_t>(ColorCodecId::LZ4);
        const int32_t cw = 32768, ch = 32768;
        const uint8_t chn = 3;
        const uint32_t raw = 3u << 30, comp = raw / 255 + 1;
        istringstream in = fieldStream({{&cw, 4}, {&ch, 4}, {&chn, 1}, {&raw, 4}, {&comp, 4}});
        DepthFrame dst;
        vector<uint8_t> scratch;
        uint64_t used = 0;
        const char* why = nullptr;
        const bool ok = tcd_detail::parseColorPayload(in, h, 17 + comp, 17 + comp, dst,
                                                      scratch, used, why);
        check(base + "/color: refused before allocating",
              !ok && why && string(why).find("too large to decode") != string::npos &&
              scratch.capacity() == 0 && !dst.color.isAllocated());
    }
}

// -----------------------------------------------------------------------------
// Reopening one object
// -----------------------------------------------------------------------------

// Each open warns about its first skipped block again, and an open that fails
// leaves no manifest from the file opened before.
static void runReopen(const filesystem::path& dir) {
    const string base = "reopen";
    if (!selected(base)) return;
    const filesystem::path path = dir / "reopen.tcdc";
    if (!writeRecording(path, Dims{}, DepthCodecId::HiloLZ4, ColorCodecId::LZ4)) {
        check(base + ": write source recording", false);
        return;
    }
    vector<uint8_t> bytes = readAll(path);
    const BlockAt k = blockOf(layoutOf(bytes), 1, BLOCK_DEPTH);
    put32(bytes, k.payload() + D_RAW, get32(bytes, k.payload() + D_RAW) + 2);
    writeAll(path, bytes);

    LogCapture cap;
    ProbePlayback p(path.string());
    p.enableDepth();
    p.enableColor();
    p.setLoop(false);
    auto playAll = [&p] {
        for (int i = 0; i < FRAMES; ++i) p.update();
    };
    auto warnings = [&cap] {
        int n = 0;
        for (const string& line : cap.lines) {
            if (line.find("byte size doesn't match the sample count") != string::npos) ++n;
        }
        return n;
    };

    const bool first = p.setup();
    playAll();
    p.close();
    const int afterFirst = warnings();
    const bool second = p.setup();
    playAll();
    p.close();
    check(base + ": each open warns once", first && second && afterFirst == 1 &&
                                           warnings() == 2 && cap.lines.size() == 2);

    const bool hadManifest = !p.getBlockTypes().empty();
    filesystem::remove(path);
    const bool third = p.setup();
    check(base + ": failed reopen leaves the manifest empty",
          hadManifest && !third && p.getBlockTypes().empty() && !p.hasUnknownBlocks() &&
          !p.hasBlockType(BLOCK_DEPTH));
}

// Whether this process has `path` open. Read from /proc/self/fd on Linux;
// elsewhere it isn't checked and counts as closed.
static bool fileHeldOpen(const filesystem::path& path) {
#if defined(__linux__)
    error_code ec;
    const filesystem::path want = filesystem::canonical(path, ec);
    if (ec) return false;
    for (const auto& fd : filesystem::directory_iterator("/proc/self/fd", ec)) {
        error_code lec;
        const filesystem::path target = filesystem::read_symlink(fd.path(), lec);
        if (!lec && target == want) return true;
    }
#else
    (void)path;
#endif
    return false;
}

// An open refused for the file's contents doesn't keep the file open, and the
// same object then opens a valid file at the same path. setup() twice without
// close() in between opens the file again.
static void runReopenAfterRefusal(const filesystem::path& dir) {
    struct Case { const char* name; function<void(vector<uint8_t>&)> apply; };
    const Case cases[] = {
        {"magicWrong", [](vector<uint8_t>& b) { b[0] = 'X'; }},
        {"manifestOverLimit",
         [](vector<uint8_t>& b) { b[offsetof(TcdcHeader, streamTypeCount)] = 200; }},
        {"frameSizeOutOfRange",
         [](vector<uint8_t>& b) { put32(b, offsetof(TcdcHeader, width), 0xFFFFFFC0u); }},
    };
    for (const Case& c : cases) {
        const string base = string("reopenAfterRefusal/") + c.name;
        if (!selected(base)) continue;
        const filesystem::path path = dir / (string("reopenAfterRefusal-") + c.name + ".tcdc");
        if (!writeRecording(path, Dims{}, DepthCodecId::HiloLZ4, ColorCodecId::LZ4)) {
            check(base + ": write source recording", false);
            continue;
        }
        const vector<uint8_t> valid = readAll(path);
        vector<uint8_t> bytes = valid;
        c.apply(bytes);
        writeAll(path, bytes);

        LogCapture cap;
        ProbePlayback p(path.string());
        p.enableDepth();
        p.enableColor();
        p.setLoop(false);
        const bool refused = !p.setup();
        check(base + ": refused open doesn't keep the file open",
              refused && !fileHeldOpen(path));

        writeAll(path, valid);
        const bool opened = p.setup();
        p.update();
        const bool reopened = opened && p.getFrameCount() == FRAMES && p.isFrameNew() &&
                              p.isColorFrameNew() && cap.lines.size() == 1;
        check(base + ": valid file opens on the same object", reopened);
        if (!reopened) {
            for (const string& line : cap.lines) printf("  logged: %s\n", line.c_str());
        }
        p.close();
    }

    const string base = "reopenAfterRefusal/setupTwice";
    if (!selected(base)) return;
    const filesystem::path path = dir / "reopenAfterRefusal-setupTwice.tcdc";
    if (!writeRecording(path, Dims{}, DepthCodecId::HiloLZ4, ColorCodecId::LZ4)) {
        check(base + ": write source recording", false);
        return;
    }
    LogCapture cap;
    ProbePlayback p(path.string());
    p.enableDepth();
    p.setLoop(false);
    const bool first = p.setup();
    const bool second = p.setup();
    p.update();
    check(base + ": second setup() opens again",
          first && second && p.getFrameCount() == FRAMES && p.isFrameNew() &&
          cap.lines.empty());
    p.close();
}

// -----------------------------------------------------------------------------
// Image converters (tcxDepthCamera's depthToImage / colorToImage / irToImage)
// -----------------------------------------------------------------------------

// Pixel i of an RGBA Image is (g, g, g, 255).
static bool grayAt(Image& img, size_t i, unsigned char g) {
    const unsigned char* d = img.getPixelsData();
    return d[i * 4 + 0] == g && d[i * 4 + 1] == g && d[i * 4 + 2] == g && d[i * 4 + 3] == 255;
}

static bool grayPixels(Image& img, int w, int h, const vector<unsigned char>& want) {
    if (!img.isAllocated() || img.getWidth() != w || img.getHeight() != h ||
        img.getChannels() != 4) {
        return false;
    }
    for (size_t i = 0; i < want.size(); ++i) {
        if (!grayAt(img, i, want[i])) return false;
    }
    return true;
}

// Normal frames convert to the expected pixels: into a new Image, into one of
// another size (reallocated) and into one of the same size (reused).
static void runImageConverters() {
    namespace dc = tcx::depthcamera;
    const string base = "imageConverters";

    if (selected(base + "/depth")) {
        LogCapture cap;
        DepthFrame f;
        f.w = 3; f.h = 2; f.depthScale = 0.001f;
        // invalid, nearer than nearM, beyond farM, 1/2, 1/4 and 3/4 of the way
        f.depth = {0, 100, 9000, 2150, 1225, 3075};
        const vector<unsigned char> want = {0, 255, 0, 127, 191, 63};
        Image fresh, other, same;
        other.allocate(5, 7, 4);
        same.allocate(3, 2, 4);
        dc::depthToImage(f, fresh);
        dc::depthToImage(f, other);
        dc::depthToImage(f, same);
        check(base + "/depth: new Image", grayPixels(fresh, 3, 2, want));
        check(base + "/depth: Image of another size", grayPixels(other, 3, 2, want));
        check(base + "/depth: Image of the same size", grayPixels(same, 3, 2, want));
        check(base + "/depth: nothing logged", cap.lines.empty());
        for (const string& line : cap.lines) printf("  logged: %s\n", line.c_str());
    }

    if (selected(base + "/color")) {
        LogCapture cap;
        Pixels c;
        c.allocate(3, 2, 4);
        const size_t bytes = static_cast<size_t>(3) * 2 * 4;
        for (size_t i = 0; i < bytes; ++i) c.getData()[i] = static_cast<unsigned char>(i * 37 + 1);
        Image fresh, other, same;
        other.allocate(5, 7, 4);
        same.allocate(3, 2, 4);
        dc::colorToImage(c, fresh);
        dc::colorToImage(c, other);
        dc::colorToImage(c, same);
        auto copied = [&](Image& img) {
            return img.isAllocated() && img.getWidth() == 3 && img.getHeight() == 2 &&
                   img.getChannels() == 4 &&
                   memcmp(img.getPixelsData(), c.getData(), bytes) == 0;
        };
        check(base + "/color: new Image", copied(fresh));
        check(base + "/color: Image of another size", copied(other));
        check(base + "/color: Image of the same size", copied(same));
        check(base + "/color: nothing logged", cap.lines.empty());
        for (const string& line : cap.lines) printf("  logged: %s\n", line.c_str());
    }

    if (selected(base + "/ir")) {
        LogCapture cap;
        Pixels ir;
        ir.allocate(3, 2, 1, PixelFormat::F32);
        // Normalized by the max (4): 0, 1/4, 1, below 0, 9/16, 1/8; then sqrt.
        const float src[] = {0.0f, 1.0f, 4.0f, -1.0f, 2.25f, 0.5f};
        memcpy(ir.getDataF32(), src, sizeof(src));
        const vector<unsigned char> want = {0, 127, 255, 0, 191, 90};
        Image fresh, other, same;
        other.allocate(5, 7, 4);
        same.allocate(3, 2, 4);
        dc::irToImage(ir, fresh);
        dc::irToImage(ir, other);
        dc::irToImage(ir, same);
        check(base + "/ir: new Image", grayPixels(fresh, 3, 2, want));
        check(base + "/ir: Image of another size", grayPixels(other, 3, 2, want));
        check(base + "/ir: Image of the same size", grayPixels(same, 3, 2, want));

        // A frame whose max is below 1 is scaled by 1, not stretched.
        Pixels dim;
        dim.allocate(2, 1, 1, PixelFormat::F32);
        dim.getDataF32()[0] = 0.25f;
        dim.getDataF32()[1] = 0.0f;
        Image dimImg;
        dc::irToImage(dim, dimImg);
        check(base + "/ir: max below 1 is not stretched", grayPixels(dimImg, 2, 1, {127, 0}));
        check(base + "/ir: nothing logged", cap.lines.empty());
        for (const string& line : cap.lines) printf("  logged: %s\n", line.c_str());
    }
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc > 1) g_only = argv[1];

    const auto stamp = chrono::steady_clock::now().time_since_epoch().count();
    const filesystem::path dir = filesystem::temp_directory_path() /
                                 ("tcxDepthRecord-tests-" + to_string(stamp));
    filesystem::create_directories(dir);

    // ----- 1. round trip ------------------------------------------------------
    struct RoundTrip {
        const char* name; DepthCodecId dc; ColorCodecId cc; int chn;
        int w = 64, h = 48, cw = 80, ch = 60;
    };
    const RoundTrip trips[] = {
        {"hiloLz4-lz4-rgba",  DepthCodecId::HiloLZ4, ColorCodecId::LZ4, 4},
        {"lz4-raw-rgb",       DepthCodecId::LZ4,     ColorCodecId::Raw, 3},
        {"raw-lz4-gray",      DepthCodecId::Raw,     ColorCodecId::LZ4, 1},
        {"hiloLz4-depthOnly", DepthCodecId::HiloLZ4, ColorCodecId::LZ4, 0},
        // A camera-sized recording (depth 1280x720, color 1920x1080).
        {"hiloLz4-lz4-rgba-720p", DepthCodecId::HiloLZ4, ColorCodecId::LZ4, 4,
         1280, 720, 1920, 1080},
    };
    for (const RoundTrip& t : trips) {
        const string base = string("roundTrip/") + t.name;
        if (!selected(base)) continue;
        Dims d;
        d.chn = t.chn;
        d.w = t.w; d.h = t.h; d.cw = t.cw; d.ch = t.ch;
        const filesystem::path path = dir / (string(t.name) + ".tcdc");
        check(base + ": record", writeRecording(path, d, t.dc, t.cc));
        const Played r = play(path);
        check(base + ": open", r.opened && r.frames.size() == FRAMES);
        if (!r.opened || r.frames.size() != FRAMES) continue;
        for (int k = 0; k < FRAMES; ++k) {
            check(base + ": frame " + to_string(k) + " intact", frameIntact(r.frames[k], d, k));
        }
        const vector<uint8_t> types = t.chn > 0
            ? vector<uint8_t>{BLOCK_DEPTH, BLOCK_COLOR, CUSTOM_TYPE}
            : vector<uint8_t>{BLOCK_DEPTH, CUSTOM_TYPE};
        check(base + ": stream manifest", r.blockTypes == types);
        check(base + ": nothing logged", r.log.empty());
        for (const string& line : r.log) printf("  logged: %s\n", line.c_str());

        const vector<uint8_t> bytes = readAll(path);
        check(base + ": every block walks by length", walksByLength(bytes, types));

        // The same file with every color block length written the old way.
        if (t.chn == 0) continue;
        vector<uint8_t> legacy = bytes;
        const vector<vector<BlockAt>> layout = layoutOf(legacy);
        for (int k = 0; k < FRAMES; ++k) {
            const BlockAt& blk = blockOf(layout, k, BLOCK_COLOR);
            put32(legacy, blk.at + 1, 13 + get32(legacy, blk.payload() + C_COMP));
        }
        const filesystem::path legacyPath = dir / (string(t.name) + "-legacyColorLen.tcdc");
        writeAll(legacyPath, legacy);
        const Played old = play(legacyPath);
        bool oldOk = old.opened && old.frames.size() == FRAMES && old.blockTypes == types;
        for (int k = 0; oldOk && k < FRAMES; ++k) oldOk = frameIntact(old.frames[k], d, k);
        check(base + ": old color block length plays", oldOk && old.log.empty());
        for (const string& line : old.log) printf("  logged: %s\n", line.c_str());
    }

    // The official player reports the custom block as unknown.
    if (selected("roundTrip/unknownBlocks")) {
        const filesystem::path path = dir / "unknown.tcdc";
        writeRecording(path, Dims{}, DepthCodecId::HiloLZ4, ColorCodecId::LZ4);
        PlaybackDepthCamera p(path.string());
        const bool opened = p.setup();
        check("roundTrip/unknownBlocks: official player sees an unknown stream",
              opened && p.hasUnknownBlocks() && p.hasBlockType(CUSTOM_TYPE));
        p.close();
    }

    // ----- 2. block sizes -----------------------------------------------------
    for (const Mutation& m : mutations()) runMutation(m, dir, Dims{});

    // ----- 3. header frame size ----------------------------------------------
    runHeaderSize(dir);

    // ----- 4. byte size above INT_MAX -----------------------------------------
    runByteSizeAboveIntMax();

    // ----- 5. reopen ----------------------------------------------------------
    runReopen(dir);
    runReopenAfterRefusal(dir);

    // ----- 6. depthToImage ----------------------------------------------------
    if (selected("depthToImageShortPlane")) {
        DepthFrame f;
        f.w = 64; f.h = 48;
        f.depth.assign(100, 1000);
        Image img;
        tcx::depthcamera::depthToImage(f, img);
        check("depthToImageShortPlane: short depth plane draws nothing", !img.isAllocated());
    }

    // ----- 7. image converters ------------------------------------------------
    runImageConverters();

    error_code ec;
    filesystem::remove_all(dir, ec);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
