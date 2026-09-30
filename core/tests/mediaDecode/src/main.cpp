// =============================================================================
// core/tests/mediaDecode — the bundled decoders read every image format and
// Ogg Vorbis through the TrussC entry points.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// Fixtures are made at runtime: stb_image_write writes PNG / JPEG / BMP / TGA /
// HDR, the rest (16-bit PNG, paletted BMP, GIF, PNM) is assembled byte by byte
// below. The Ogg clip is embedded (toneOgg.h), since nothing here can encode
// Vorbis.
//
// Guards the invariants:
// - Pixels::loadFromMemory() decodes PNG (8-bit and 16-bit), JPEG, BMP
//   (24-bit and 8-bit paletted), TGA (raw and RLE), HDR, GIF (the first frame
//   of a two-frame file) and PNM to RGBA with the expected size and pixels.
// - Pixels::load() and Pixels::loadHDR() read the same files from disk.
// - A paletted BMP whose pixels index past the stored palette entries reads
//   those pixels as black (TrussC patch in stb_image.h).
// - A GIF whose LZW prefix chains are as long as the format allows decodes
//   with a small stack (TrussC patch in stb_image.h: the chain is walked with
//   a loop, not recursion).
//   Native POSIX builds run it on a thread with a 64 KB stack; Windows and
//   the web build decode it on the calling thread, which only checks the
//   output there (the wasm call stack is not the 64 KB data stack).
// - SoundBuffer::loadOgg() and loadOggFromMemory() report the clip's channel
//   count, rate and length, and decode its samples.
// =============================================================================

#include <TrussC.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#define TC_TEST_SMALL_STACK_THREAD 1
#endif

#include "toneOgg.h"

using namespace std;
using namespace tc;

using Bytes = vector<unsigned char>;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// --- Byte helpers --------------------------------------------------------------
static void put16le(Bytes& b, uint32_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
static void put32le(Bytes& b, uint32_t v) { put16le(b, v & 0xFFFF); put16le(b, v >> 16); }
static void put32be(Bytes& b, uint32_t v) {
    b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF);  b.push_back(v & 0xFF);
}
static void putStr(Bytes& b, const char* s) { while (*s) b.push_back((unsigned char)*s++); }

static void writeToVector(void* ctx, void* data, int size) {
    auto* out = static_cast<Bytes*>(ctx);
    auto* p = static_cast<unsigned char*>(data);
    out->insert(out->end(), p, p + size);
}

static bool writeFile(const fs::path& path, const Bytes& b) {
    ofstream f(path, ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), (streamsize)b.size());
    return (bool)f;
}

// --- Pixel checks --------------------------------------------------------------
static string rgbaAt(const Pixels& p, int x, int y) {
    const unsigned char* d = p.getData() + ((size_t)y * p.getWidth() + x) * 4;
    return "(" + to_string(d[0]) + "," + to_string(d[1]) + "," + to_string(d[2]) + "," +
           to_string(d[3]) + ")";
}

static bool pixelNear(const Pixels& p, int x, int y, int r, int g, int b, int a, int tol) {
    if (!p.isAllocated() || p.isFloat() || p.getChannels() != 4) return false;
    if (x >= p.getWidth() || y >= p.getHeight()) return false;
    const unsigned char* d = p.getData() + ((size_t)y * p.getWidth() + x) * 4;
    return abs(d[0] - r) <= tol && abs(d[1] - g) <= tol && abs(d[2] - b) <= tol &&
           abs(d[3] - a) <= tol;
}

// Loads `bytes` from memory and checks size plus every listed pixel.
struct Px { int x, y, r, g, b, a; };
static void checkImage(const string& name, const Bytes& bytes, int w, int h,
                       const vector<Px>& expect, int tol = 0) {
    Pixels p;
    LoadResult r = p.loadFromMemory(bytes.data(), (int)bytes.size());
    check(name + ": loadFromMemory succeeds", (bool)r, r.message);
    if (!r) return;
    check(name + ": size " + to_string(w) + "x" + to_string(h),
          p.getWidth() == w && p.getHeight() == h,
          to_string(p.getWidth()) + "x" + to_string(p.getHeight()));
    for (auto& e : expect) {
        check(name + ": pixel (" + to_string(e.x) + "," + to_string(e.y) + ")",
              pixelNear(p, e.x, e.y, e.r, e.g, e.b, e.a, tol), rgbaAt(p, e.x, e.y));
    }
}

// --- PNG assembled by hand (stb_image_write only writes 8-bit) ---------------------
static uint32_t crc32(const unsigned char* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static void pngChunk(Bytes& png, const char* type, const Bytes& data) {
    put32be(png, (uint32_t)data.size());
    Bytes body;
    putStr(body, type);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    put32be(png, crc32(body.data(), body.size()));
}

// zlib stream with one stored (uncompressed) deflate block.
static Bytes zlibStored(const Bytes& raw) {
    Bytes z = {0x78, 0x01, 0x01};
    put16le(z, (uint32_t)raw.size());
    put16le(z, (uint32_t)(~raw.size() & 0xFFFF));
    z.insert(z.end(), raw.begin(), raw.end());
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32be(z, (b << 16) | a);
    return z;
}

// 16-bit PNG; `rows` holds the big-endian sample bytes of each row.
static Bytes png16(int w, int h, int colorType, const vector<Bytes>& rows) {
    Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes ihdr;
    put32be(ihdr, w); put32be(ihdr, h);
    ihdr.push_back(16); ihdr.push_back((unsigned char)colorType);
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    pngChunk(png, "IHDR", ihdr);
    Bytes raw;
    for (auto& r : rows) { raw.push_back(0); raw.insert(raw.end(), r.begin(), r.end()); }
    pngChunk(png, "IDAT", zlibStored(raw));
    pngChunk(png, "IEND", {});
    return png;
}

// --- BMP, 8-bit paletted -----------------------------------------------------------
// `palette` holds RGB triples; `indices` is top-down, width must be a multiple of 4.
static Bytes bmp8(int w, int h, const vector<array<int, 3>>& palette, const vector<int>& indices) {
    const uint32_t offset = 14 + 40 + 4 * (uint32_t)palette.size();
    Bytes b;
    putStr(b, "BM");
    put32le(b, offset + (uint32_t)(w * h));
    put32le(b, 0);
    put32le(b, offset);
    put32le(b, 40); put32le(b, w); put32le(b, h);     // positive height: rows bottom-up
    put16le(b, 1); put16le(b, 8);
    put32le(b, 0); put32le(b, w * h); put32le(b, 2835); put32le(b, 2835);
    put32le(b, (uint32_t)palette.size()); put32le(b, 0);
    for (auto& c : palette) { b.push_back(c[2]); b.push_back(c[1]); b.push_back(c[0]); b.push_back(0); }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) b.push_back((unsigned char)indices[y * w + x]);
    }
    return b;
}

// --- GIF assembled by hand ----------------------------------------------------------
// Packs LZW codes LSB-first, growing the code size exactly as stb_image's
// decoder does (after each table entry, when the next free code fills the
// current width), and splits the result into sub-blocks.
static Bytes lzwBlocks(int minCodeSize, const vector<int>& codes) {
    const int clear = 1 << minCodeSize;
    int codeSize = minCodeSize + 1;
    int avail = clear + 2;
    bool haveOld = false;
    Bytes packed;
    uint32_t acc = 0;
    int nbits = 0;
    for (int code : codes) {
        acc |= (uint32_t)code << nbits;
        nbits += codeSize;
        while (nbits >= 8) { packed.push_back(acc & 0xFF); acc >>= 8; nbits -= 8; }
        if (code == clear) {
            codeSize = minCodeSize + 1;
            avail = clear + 2;
            haveOld = false;
        } else if (code != clear + 1) {
            if (haveOld) ++avail;
            haveOld = true;
            if ((avail & ((1 << codeSize) - 1)) == 0 && avail <= 0x0FFF) ++codeSize;
        }
    }
    if (nbits > 0) packed.push_back(acc & 0xFF);

    Bytes out;
    out.push_back((unsigned char)minCodeSize);
    for (size_t i = 0; i < packed.size(); i += 255) {
        size_t n = min<size_t>(255, packed.size() - i);
        out.push_back((unsigned char)n);
        out.insert(out.end(), packed.begin() + i, packed.begin() + i + n);
    }
    out.push_back(0);
    return out;
}

// GIF89a with a 4-entry global palette and one image per entry of `frames`.
static Bytes gif(int w, int h, const vector<array<int, 3>>& palette4, const vector<vector<int>>& frames) {
    Bytes g;
    putStr(g, "GIF89a");
    put16le(g, w); put16le(g, h);
    g.push_back(0x81);   // global color table, 2^(1+1) = 4 entries
    g.push_back(0); g.push_back(0);
    for (auto& c : palette4) { g.push_back(c[0]); g.push_back(c[1]); g.push_back(c[2]); }
    for (auto& codes : frames) {
        // Graphic control extension: 10/100 s delay, no disposal, no transparency.
        const unsigned char gce[] = {0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00};
        g.insert(g.end(), gce, gce + sizeof(gce));
        g.push_back(0x2C);
        put16le(g, 0); put16le(g, 0); put16le(g, w); put16le(g, h);
        g.push_back(0);
        Bytes lzw = lzwBlocks(2, codes);
        g.insert(g.end(), lzw.begin(), lzw.end());
    }
    g.push_back(0x3B);
    return g;
}

// --- Stack painting --------------------------------------------------------------
// Fills stack memory below the caller with a non-zero byte, so the palette
// check below cannot pass just because the stack happened to be zero.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static void paintStack() {
    volatile unsigned char area[32 * 1024];
    for (size_t i = 0; i < sizeof(area); ++i) area[i] = 0xA5;
}

// --- Small-stack decode ----------------------------------------------------------
struct SmallStackJob {
    const Bytes* bytes;
    bool ok = false;
    int w = 0, h = 0;
    unsigned char rgba[4] = {0, 0, 0, 0};
};

static void* runSmallStackJob(void* arg) {
    auto* job = static_cast<SmallStackJob*>(arg);
    Pixels p;
    job->ok = (bool)p.loadFromMemory(job->bytes->data(), (int)job->bytes->size());
    if (job->ok) {
        job->w = p.getWidth();
        job->h = p.getHeight();
        for (int i = 0; i < 4; ++i) job->rgba[i] = p.getData()[i];
    }
    return nullptr;
}

// Runs the decode on a thread with `stackBytes` of stack (native POSIX), or
// on the calling thread elsewhere. Returns false if the thread could not start.
static bool decodeOnSmallStack(SmallStackJob& job, size_t stackBytes) {
#if defined(TC_TEST_SMALL_STACK_THREAD)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (pthread_attr_setstacksize(&attr, stackBytes) != 0) { pthread_attr_destroy(&attr); return false; }
    pthread_t th;
    int rc = pthread_create(&th, &attr, runSmallStackJob, &job);
    pthread_attr_destroy(&attr);
    if (rc != 0) return false;
    pthread_join(th, nullptr);
    return true;
#else
    (void)stackBytes;
    runSmallStackJob(&job);
    return true;
#endif
}

// =============================================================================

int main() {
    const string tag = to_string((long long)chrono::steady_clock::now().time_since_epoch().count());
    const fs::path tmp = fs::temp_directory_path();

    // 3x2 RGBA reference image.
    const unsigned char rgba3x2[3 * 2 * 4] = {
        255, 0, 0, 255,     0, 255, 0, 255,    0, 0, 255, 255,
        255, 255, 255, 128, 0, 0, 0, 255,      10, 200, 30, 0,
    };
    const vector<Px> rgba3x2Expect = {
        {0, 0, 255, 0, 0, 255}, {1, 0, 0, 255, 0, 255}, {2, 0, 0, 0, 255, 255},
        {0, 1, 255, 255, 255, 128}, {1, 1, 0, 0, 0, 255}, {2, 1, 10, 200, 30, 0},
    };
    // Same image without alpha (BMP 24-bit): alpha reads 255.
    unsigned char rgb3x2[3 * 2 * 3];
    for (int i = 0; i < 6; ++i) for (int c = 0; c < 3; ++c) rgb3x2[i * 3 + c] = rgba3x2[i * 4 + c];
    vector<Px> rgb3x2Expect = rgba3x2Expect;
    for (auto& e : rgb3x2Expect) e.a = 255;

    // --- PNG 8-bit --------------------------------------------------------------
    Bytes png8;
    check("png8: written", stbi_write_png_to_func(writeToVector, &png8, 3, 2, 4, rgba3x2, 3 * 4) != 0);
    checkImage("png8", png8, 3, 2, rgba3x2Expect);
    {
        const fs::path path = tmp / ("tc_mediaDecode_" + tag + ".png");
        check("png8: file written", writeFile(path, png8));
        Pixels p;
        LoadResult r = p.load(path);
        check("png8: load() from a file", (bool)r && p.getWidth() == 3 && p.getHeight() == 2 &&
                                             pixelNear(p, 2, 1, 10, 200, 30, 0, 0), r.message);
        std::error_code ec;
        fs::remove(path, ec);
    }

    // --- PNG 16-bit: grayscale 2x2 and RGB 2x1 (16 -> 8 keeps the high byte) ------
    checkImage("png16 gray", png16(2, 2, 0, {{0x00, 0x00, 0xFF, 0xFF}, {0x80, 0x00, 0x12, 0xAB}}), 2, 2,
               {{0, 0, 0, 0, 0, 255}, {1, 0, 255, 255, 255, 255},
                {0, 1, 128, 128, 128, 255}, {1, 1, 0x12, 0x12, 0x12, 255}});
    checkImage("png16 rgb",
               png16(2, 1, 2, {{0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xFF, 0xFF}}), 2, 1,
               {{0, 0, 255, 0, 0, 255}, {1, 0, 0, 128, 255, 255}});

    // --- JPEG: 16x16, four flat 8x8 quadrants, quality 100 -------------------------
    {
        const int q[4][3] = {{200, 40, 40}, {40, 200, 40}, {40, 40, 200}, {128, 128, 128}};
        vector<unsigned char> img(16 * 16 * 3);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            const int* c = q[(y / 8) * 2 + (x / 8)];
            for (int k = 0; k < 3; ++k) img[(y * 16 + x) * 3 + k] = (unsigned char)c[k];
        }
        Bytes jpg;
        check("jpeg: written", stbi_write_jpg_to_func(writeToVector, &jpg, 16, 16, 3, img.data(), 100) != 0);
        checkImage("jpeg", jpg, 16, 16,
                   {{3, 3, 200, 40, 40, 255}, {12, 3, 40, 200, 40, 255},
                    {3, 12, 40, 40, 200, 255}, {12, 12, 128, 128, 128, 255}}, 10);
    }

    // --- BMP 24-bit ---------------------------------------------------------------
    Bytes bmp24;
    check("bmp24: written", stbi_write_bmp_to_func(writeToVector, &bmp24, 3, 2, 3, rgb3x2) != 0);
    checkImage("bmp24", bmp24, 3, 2, rgb3x2Expect);

    // --- BMP 8-bit paletted, 3 stored entries; index 200 is past them ---------------
    {
        Bytes b = bmp8(4, 2, {{{255, 0, 0}}, {{0, 255, 0}}, {{20, 40, 60}}}, {0, 1, 2, 200, 2, 1, 0, 255});
        paintStack();
        checkImage("bmp8 paletted", b, 4, 2,
                   {{0, 0, 255, 0, 0, 255}, {1, 0, 0, 255, 0, 255}, {2, 0, 20, 40, 60, 255},
                    {0, 1, 20, 40, 60, 255}, {1, 1, 0, 255, 0, 255}, {2, 1, 255, 0, 0, 255}});
        checkImage("bmp8 index past the palette reads black", b, 4, 2,
                   {{3, 0, 0, 0, 0, 255}, {3, 1, 0, 0, 0, 255}});
    }

    // --- TGA, raw and RLE -----------------------------------------------------------
    {
        const int savedRle = stbi_write_tga_with_rle;
        Bytes raw, rle;
        stbi_write_tga_with_rle = 0;
        check("tga raw: written", stbi_write_tga_to_func(writeToVector, &raw, 3, 2, 4, rgba3x2) != 0);
        stbi_write_tga_with_rle = 1;
        check("tga rle: written", stbi_write_tga_to_func(writeToVector, &rle, 3, 2, 4, rgba3x2) != 0);
        stbi_write_tga_with_rle = savedRle;
        checkImage("tga raw", raw, 3, 2, rgba3x2Expect);
        checkImage("tga rle", rle, 3, 2, rgba3x2Expect);
    }

    // --- HDR: 16x2 (RLE scanlines) and 4x1 (flat scanlines) ----------------------------
    {
        // Powers of two survive RGBE exactly.
        auto hdrImage = [](int w, int h) {
            vector<float> f((size_t)w * h * 3);
            for (int i = 0; i < w * h; ++i) {
                f[i * 3 + 0] = (i % 2) ? 4.0f : 1.0f;
                f[i * 3 + 1] = (i % 2) ? 2.0f : 0.5f;
                f[i * 3 + 2] = (i % 2) ? 1.0f : 0.25f;
            }
            return f;
        };
        for (int w : {16, 4}) {
            const int h = (w == 16) ? 2 : 1;
            const string name = "hdr " + to_string(w) + "x" + to_string(h);
            vector<float> f = hdrImage(w, h);
            Bytes hdr;
            check(name + ": written", stbi_write_hdr_to_func(writeToVector, &hdr, w, h, 3, f.data()) != 0);

            const fs::path path = tmp / ("tc_mediaDecode_" + tag + "_" + to_string(w) + ".hdr");
            check(name + ": file written", writeFile(path, hdr));
            Pixels p;
            LoadResult r = p.loadHDR(path);
            check(name + ": loadHDR() succeeds", (bool)r, r.message);
            if (r) {
                check(name + ": loadHDR() size", p.getWidth() == w && p.getHeight() == h && p.isFloat());
                const float* d = p.getDataF32();
                const size_t last = ((size_t)w * h - 1) * 4;   // odd index: (4, 2, 1)
                check(name + ": loadHDR() values",
                      d[0] == 1.0f && d[1] == 0.5f && d[2] == 0.25f && d[3] == 1.0f &&
                          d[4] == 4.0f && d[5] == 2.0f && d[6] == 1.0f &&
                          d[last] == 4.0f && d[last + 1] == 2.0f && d[last + 2] == 1.0f,
                      to_string(d[0]) + " " + to_string(d[1]) + " " + to_string(d[2]) + " / " +
                          to_string(d[4]) + " " + to_string(d[5]) + " " + to_string(d[6]));
            }
            std::error_code ec;
            fs::remove(path, ec);

            // From memory it is tone-mapped to 8 bits (gamma 2.2, values >= 1 clamp to 255).
            checkImage(name, hdr, w, h, {{0, 0, 255, 186, 136, 255}, {1, 0, 255, 255, 255, 255}}, 2);
        }
    }

    // --- GIF: two frames, 4x2; the first frame reads -----------------------------------
    {
        const vector<array<int, 3>> pal = {{{255, 0, 0}}, {{0, 255, 0}}, {{0, 0, 255}}, {{250, 250, 250}}};
        // Frame 1: literals 0 1 2 3 (entries 6="01" 7="12" 8="23"), then code 6
        // ("01") and code 8 ("23"): pixels 0 1 2 3 / 0 1 2 3, so two-link
        // strings come out in order.
        const vector<int> frame1 = {4, 0, 1, 2, 3, 6, 8, 5};
        const vector<int> frame2 = {4, 3, 3, 3, 3, 3, 3, 3, 3, 5};
        vector<Px> expect;
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
            auto& c = pal[x];
            expect.push_back({x, y, c[0], c[1], c[2], 255});
        }
        checkImage("gif 2 frames (first frame)", gif(4, 2, pal, {frame1, frame2}), 4, 2, expect);
        checkImage("gif 1 frame", gif(4, 2, pal, {frame1}), 4, 2, expect);
    }

    // --- GIF with the longest LZW prefix chains, decoded on a 64 KB stack ------------------
    {
        // After the first literal, each code is the entry being defined (the
        // "KwKwK" case), so every entry extends the previous one by a link:
        // entry 4095 ends a chain of about 4090 links.
        vector<int> codes = {4, 0};
        for (int c = 6; c <= 4095; ++c) codes.push_back(c);
        codes.push_back(5);
        const Bytes deep = gif(2, 2, {{{30, 60, 90}}, {{0, 0, 0}}, {{0, 0, 0}}, {{0, 0, 0}}}, {codes});
        SmallStackJob job;
        job.bytes = &deep;
        const bool ran = decodeOnSmallStack(job, 64 * 1024);
        check("gif long LZW chains: decode thread ran", ran);
#if defined(TC_TEST_SMALL_STACK_THREAD)
        const string where = "on a 64 KB thread stack";
#else
        const string where = "on the calling thread";
#endif
        check("gif long LZW chains: decodes " + where,
              ran && job.ok && job.w == 2 && job.h == 2 && job.rgba[0] == 30 && job.rgba[1] == 60 &&
                  job.rgba[2] == 90 && job.rgba[3] == 255,
              to_string(job.w) + "x" + to_string(job.h) + " first pixel " + to_string(job.rgba[0]) + "," +
                  to_string(job.rgba[1]) + "," + to_string(job.rgba[2]) + "," + to_string(job.rgba[3]));
    }

    // --- PNM: 8-bit PPM and 16-bit PGM ------------------------------------------------
    {
        Bytes ppm;
        putStr(ppm, "P6\n2 1\n255\n");
        for (int v : {255, 128, 0, 1, 2, 3}) ppm.push_back((unsigned char)v);
        checkImage("ppm 8-bit", ppm, 2, 1, {{0, 0, 255, 128, 0, 255}, {1, 0, 1, 2, 3, 255}});
        Bytes pgm;
        putStr(pgm, "P5\n2 1\n65535\n");
        // Both bytes of each sample are equal: stb_image reads 16-bit PNM
        // samples in host byte order, so only such values are portable here.
        for (int v : {0xFF, 0xFF, 0x80, 0x80}) pgm.push_back((unsigned char)v);
        checkImage("pgm 16-bit", pgm, 2, 1, {{0, 0, 255, 255, 255, 255}, {1, 0, 128, 128, 128, 255}});
    }

    // --- Ogg Vorbis: file and memory ------------------------------------------------------
    {
        auto checkTone = [](const string& name, const SoundBuffer& sb) {
            check(name + ": 2 channels, 8000 Hz", sb.channels == 2 && sb.sampleRate == 8000,
                  to_string(sb.channels) + " ch, " + to_string(sb.sampleRate) + " Hz");
            check(name + ": 4000 frames", sb.numSamples == 4000 && sb.samples.size() == 8000,
                  to_string(sb.numSamples) + " frames, " + to_string(sb.samples.size()) + " samples");
            double l = 0, r = 0;
            const size_t n = min<size_t>(sb.numSamples, sb.samples.size() / 2);
            for (size_t i = 0; i < n; ++i) {
                l += sb.samples[i * 2] * sb.samples[i * 2];
                r += sb.samples[i * 2 + 1] * sb.samples[i * 2 + 1];
            }
            const double rmsL = n ? sqrt(l / n) : 0, rmsR = n ? sqrt(r / n) : 0;
            // A sine at amplitude A has RMS A / sqrt(2): 0.354 left, 0.177 right.
            check(name + ": channel levels", fabs(rmsL - 0.354) < 0.04 && fabs(rmsR - 0.177) < 0.03,
                  "rms " + to_string(rmsL) + " / " + to_string(rmsR));
        };

        SoundBuffer mem;
        LoadResult r = mem.loadOggFromMemory(kToneOgg, sizeof(kToneOgg));
        check("ogg: loadOggFromMemory succeeds", (bool)r, r.message);
        if (r) checkTone("ogg memory", mem);

        const fs::path path = tmp / ("tc_mediaDecode_" + tag + ".ogg");
        check("ogg: file written", writeFile(path, Bytes(kToneOgg, kToneOgg + sizeof(kToneOgg))));
        SoundBuffer file;
        r = file.loadOgg(path);
        check("ogg: loadOgg succeeds", (bool)r, r.message);
        if (r) checkTone("ogg file", file);
        std::error_code ec;
        fs::remove(path, ec);
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
