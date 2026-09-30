// =============================================================================
// tcxDepthRecord tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR via examples/build_all.py
// --addon-tests-only (exit 0 = pass, non-zero = fail). Console only.
//
//   1. Round trip: recordings written by DepthRecorder (every depth / color
//      codec, 1/3/4-channel color, depth only, a custom block per frame, and
//      frames that are smooth, incompressible and all zero) play back
//      byte-for-byte, with no warnings.
//   2. Block sizes: copies of a valid recording with one field of the middle
//      frame changed (depth sample count / byte size / compressed size, color
//      width / height / channels / byte size / compressed size, a block
//      length, an index offset, data that doesn't decode, the stream manifest
//      count, the header magic) are skipped or refused with the expected
//      message, and the other frames still play.
//   3. depthToImage() draws nothing for a depth plane shorter than w*h.
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
#include <memory>
#include <string>
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
            r.frames.push_back(move(pf));
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

// Blocks of each frame, read from a valid file. Depth and color payloads end
// where their own size fields say: DepthRecorder writes a color block's length
// as 13 + compressed size, though its fields take 17 bytes.
static vector<vector<BlockAt>> layoutOf(const vector<uint8_t>& b) {
    TcdcHeader h;
    memcpy(&h, b.data(), sizeof(h));
    vector<uint64_t> offsets;
    for (uint32_t i = 0; i < h.frameCount; ++i) {
        uint64_t off = 0;
        memcpy(&off, b.data() + h.indexOffset + i * 16 + 8, 8);
        offsets.push_back(off);
    }
    vector<vector<BlockAt>> frames;
    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint64_t end = i + 1 < offsets.size() ? offsets[i + 1] : h.indexOffset;
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
        {"depthLenBelowFields", hilo, lz4, X, X, X, "shorter than its size fields",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.at + 1, 4); }},
        {"depthLenPastFrame", hilo, lz4, X, X, X, "runs past the end of its frame",
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_DEPTH);
                                put32(b, k.at + 1, 0xFFFFFFF0u); }},

        // --- color block ---
        // The length counting all 17 bytes of fields plays like the length
        // DepthRecorder writes (13 + compressed size).
        {"colorLenCountsAllFields", hilo, lz4, R, R, R, nullptr,
         [](auto& b, auto& L) { const auto& k = blockOf(L, 1, BLOCK_COLOR);
                                put32(b, k.at + 1, k.len + 4); }},
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
                                put32(b, k.payload() + C_COMP, k.len - 13 + 1); }},
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
                                const uint64_t far = uint64_t(1) << 40;
                                memcpy(b.data() + h.indexOffset + 2 * 16 + 8, &far, 8);
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

int main(int argc, char** argv) {
    if (argc > 1) g_only = argv[1];

    const auto stamp = chrono::steady_clock::now().time_since_epoch().count();
    const filesystem::path dir = filesystem::temp_directory_path() /
                                 ("tcxDepthRecord-tests-" + to_string(stamp));
    filesystem::create_directories(dir);

    // ----- 1. round trip ------------------------------------------------------
    struct RoundTrip { const char* name; DepthCodecId dc; ColorCodecId cc; int chn; };
    const RoundTrip trips[] = {
        {"hiloLz4-lz4-rgba",  DepthCodecId::HiloLZ4, ColorCodecId::LZ4, 4},
        {"lz4-raw-rgb",       DepthCodecId::LZ4,     ColorCodecId::Raw, 3},
        {"raw-lz4-gray",      DepthCodecId::Raw,     ColorCodecId::LZ4, 1},
        {"hiloLz4-depthOnly", DepthCodecId::HiloLZ4, ColorCodecId::LZ4, 0},
    };
    for (const RoundTrip& t : trips) {
        const string base = string("roundTrip/") + t.name;
        if (!selected(base)) continue;
        Dims d;
        d.chn = t.chn;
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

    // ----- 3. depthToImage ----------------------------------------------------
    if (selected("depthToImageShortPlane")) {
        DepthFrame f;
        f.w = 64; f.h = 48;
        f.depth.assign(100, 1000);
        Image img;
        tcx::depthcamera::depthToImage(f, img);
        check("depthToImageShortPlane: short depth plane draws nothing", !img.isAllocated());
    }

    error_code ec;
    filesystem::remove_all(dir, ec);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
