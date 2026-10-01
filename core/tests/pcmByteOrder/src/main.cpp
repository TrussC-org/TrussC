// =============================================================================
// pcmByteOrder — SoundBuffer::loadPcmFromMemory() decodes both byte orders
//
// - 16-bit: known byte pairs, little- and big-endian, with bytes of 0x80 and
//   above in either position, give exactly value / 32768 (#419: the old
//   big-endian swap shifted a signed value and sign-extended, so `01 80`
//   came out as -128 instead of 384).
// - 32-bit float: known byte quads, both byte orders, with bytes of 0x80 and
//   above in every position, give exactly the same bits.
// - The data pointer need not be aligned to the sample size.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

// Big-endian byte pairs and the int16 value they encode
struct Pair16 {
    uint8_t hi, lo;
    int16_t value;
};
static const Pair16 kPairs[] = {
    {0x00, 0x00, 0},
    {0x00, 0x01, 1},
    {0x01, 0x80, 384},      // low byte >= 0x80 (the #419 example)
    {0x00, 0xFF, 255},      // low byte >= 0x80
    {0x7F, 0xFF, 32767},    // low byte >= 0x80
    {0x80, 0x00, -32768},   // high byte >= 0x80
    {0x80, 0x01, -32767},   // high byte >= 0x80
    {0xFF, 0x80, -128},     // both >= 0x80
    {0xFF, 0xFF, -1},       // both >= 0x80
    {0x12, 0x34, 4660},
    {0xC0, 0x00, -16384},
    {0x40, 0x00, 16384},
};
static const size_t kNumPairs = sizeof(kPairs) / sizeof(kPairs[0]);

// Float bit patterns with bytes >= 0x80 in each position
static const uint32_t kFloatBits[] = {
    0x3F800000u,  // 1.0 (second byte)
    0xBF000000u,  // -0.5 (first byte)
    0x3F800080u,  // second and last byte
    0x3F008000u,  // third byte
    0x41C80000u,  // 25.0 (second byte)
    0x00800080u,  // small normal with a high last byte
    0x7F7FFF80u,  // near FLT_MAX
    0x80000000u,  // -0.0
    0xC2F6E979u,  // -123.456
    0x3EAAAAABu,  // 1/3
};
static const size_t kNumFloats = sizeof(kFloatBits) / sizeof(kFloatBits[0]);

static string hex16(uint8_t a, uint8_t b) {
    char s[16];
    snprintf(s, sizeof(s), "%02X %02X", a, b);
    return s;
}

static string hex32(uint32_t v) {
    char s[16];
    snprintf(s, sizeof(s), "%08X", (unsigned)v);
    return s;
}

static uint32_t floatBits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

// --- 16-bit ------------------------------------------------------------------
static void check16(bool bigEndian, size_t misalign) {
    const string order = bigEndian ? "big-endian" : "little-endian";
    const string tag = misalign ? " (offset " + to_string(misalign) + ")" : "";

    // One buffer with every pair, offset by misalign bytes
    vector<uint8_t> raw(misalign + kNumPairs * 2, 0xEE);
    for (size_t i = 0; i < kNumPairs; i++) {
        uint8_t* p = raw.data() + misalign + i * 2;
        p[0] = bigEndian ? kPairs[i].hi : kPairs[i].lo;
        p[1] = bigEndian ? kPairs[i].lo : kPairs[i].hi;
    }

    SoundBuffer buf;
    const bool ok = (bool)buf.loadPcmFromMemory(raw.data() + misalign, kNumPairs * 2, 1, 48000,
                                                16, bigEndian);
    check("16-bit " + order + tag + ": loads", ok && buf.numSamples == kNumPairs &&
          buf.samples.size() == kNumPairs);
    if (!ok || buf.samples.size() != kNumPairs) return;

    for (size_t i = 0; i < kNumPairs; i++) {
        const uint8_t* p = raw.data() + misalign + i * 2;
        const float expected = kPairs[i].value / 32768.0f;
        check("16-bit " + order + tag + ": bytes " + hex16(p[0], p[1]) + " -> " +
              to_string(kPairs[i].value),
              buf.samples[i] == expected,
              "got " + to_string(buf.samples[i] * 32768.0f));
    }
}

// Interleaved stereo, big-endian: channel order and values survive the swap
static void check16Stereo() {
    const uint8_t be[] = {0x01, 0x80, 0xFF, 0x80,   // frame 0: 384, -128
                          0x80, 0x01, 0x00, 0xFF};  // frame 1: -32767, 255
    SoundBuffer buf;
    const bool ok = (bool)buf.loadPcmFromMemory(be, sizeof(be), 2, 44100, 16, true);
    check("16-bit big-endian stereo: frames and channel order",
          ok && buf.numSamples == 2 && buf.channels == 2 && buf.samples.size() == 4 &&
          buf.samples[0] == 384 / 32768.0f && buf.samples[1] == -128 / 32768.0f &&
          buf.samples[2] == -32767 / 32768.0f && buf.samples[3] == 255 / 32768.0f);
}

// --- 32-bit float ------------------------------------------------------------
static void check32(bool bigEndian, size_t misalign) {
    const string order = bigEndian ? "big-endian" : "little-endian";
    const string tag = misalign ? " (offset " + to_string(misalign) + ")" : "";

    vector<uint8_t> raw(misalign + kNumFloats * 4, 0xEE);
    for (size_t i = 0; i < kNumFloats; i++) {
        uint8_t* p = raw.data() + misalign + i * 4;
        const uint32_t v = kFloatBits[i];
        for (int b = 0; b < 4; b++) {
            const int shift = bigEndian ? (24 - 8 * b) : (8 * b);
            p[b] = static_cast<uint8_t>(v >> shift);
        }
    }

    SoundBuffer buf;
    const bool ok = (bool)buf.loadPcmFromMemory(raw.data() + misalign, kNumFloats * 4, 1, 48000,
                                                32, bigEndian);
    check("32-bit float " + order + tag + ": loads", ok && buf.numSamples == kNumFloats &&
          buf.samples.size() == kNumFloats);
    if (!ok || buf.samples.size() != kNumFloats) return;

    for (size_t i = 0; i < kNumFloats; i++) {
        const uint32_t got = floatBits(buf.samples[i]);
        check("32-bit float " + order + tag + ": bits " + hex32(kFloatBits[i]),
              got == kFloatBits[i], "got " + hex32(got));
    }
}

} // namespace

TC_CORE_TEST_MAIN() {
    for (size_t misalign : {(size_t)0, (size_t)1}) {
        check16(false, misalign);
        check16(true, misalign);
    }
    check16Stereo();
    for (size_t misalign : {(size_t)0, (size_t)1, (size_t)3}) {
        check32(false, misalign);
        check32(true, misalign);
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
