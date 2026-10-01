// =============================================================================
// tcxHap tests — PCM audio byte order (#419)
//
// Four HAP movies with the same -6 dB 440 Hz sine in different PCM formats
// (bin/data, made with ffmpeg, see README.md) are parsed with MovParser and
// decoded with loadPcmTrack(), the path HapPlayer uses:
// - 'sowt' (16-bit little-endian) is the reference;
// - 'twos' (16-bit big-endian) matches it exactly;
// - 'fl32' with 'enda' = 0 (big-endian) and 'fl32' with 'enda' = 1
//   (little-endian) match it within 1/32768;
// - 'fl32' with the 'enda' atom removed is big-endian (the QuickTime default)
//   and matches too;
// - 'fl32' with 'enda' = 1 inside a 'wave' nested in 'wave' is read as
//   big-endian ('wave' is read one level deep) and matches too.
// Headless: no window, no GPU, no audio device.
// =============================================================================

#include <TrussC.h>
#include "tcxHapPlayer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace std;
using namespace tc;
using namespace tcx::hap;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

struct Decoded {
    bool ok = false;
    uint32_t fourcc = 0;
    bool bigEndian = false;
    bool floatPcm = false;
    SoundBuffer buffer;
};

static Decoded decode(const fs::path& path) {
    Decoded d;
    MovParser parser;
    if (!parser.open(path)) return d;
    const MovTrack* track = parser.getInfo().getAudioTrack();
    if (!track) return d;
    d.fourcc = track->codecFourCC;
    d.bigEndian = track->isBigEndianPcm();
    d.floatPcm = track->isFloatPcm();
    d.ok = loadPcmTrack(parser, *track, d.buffer);
    return d;
}

// Largest absolute difference to the reference, or -1 when the shapes differ
static double maxDiff(const SoundBuffer& a, const SoundBuffer& ref) {
    if (a.channels != ref.channels || a.sampleRate != ref.sampleRate ||
        a.numSamples != ref.numSamples || a.samples.size() != ref.samples.size()) {
        return -1.0;
    }
    double m = 0.0;
    for (size_t i = 0; i < a.samples.size(); i++) {
        m = max(m, (double)fabs(a.samples[i] - ref.samples[i]));
    }
    return m;
}

static void peakRms(const SoundBuffer& b, double& peak, double& rms) {
    peak = 0.0;
    double sum = 0.0;
    for (float s : b.samples) {
        peak = max(peak, (double)fabs(s));
        sum += (double)s * s;
    }
    rms = b.samples.empty() ? 0.0 : sqrt(sum / b.samples.size());
}

// Copy of a movie whose 'enda' atoms are renamed to an unknown type, so the
// sound description has no 'enda' at all
static bool writeWithoutEnda(const fs::path& src, const fs::path& dst, int& renamed) {
    ifstream in(src, ios::binary);
    if (!in) return false;
    vector<char> bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    renamed = 0;
    for (size_t i = 4; i + 4 <= bytes.size(); i++) {
        if (memcmp(&bytes[i], "enda", 4) == 0) {
            memcpy(&bytes[i], "xnda", 4);
            renamed++;
        }
    }
    ofstream out(dst, ios::binary);
    out.write(bytes.data(), (streamsize)bytes.size());
    return (bool)out;
}

static uint32_t getU32(const vector<char>& b, size_t pos) {
    return (uint32_t(uint8_t(b[pos])) << 24) | (uint32_t(uint8_t(b[pos + 1])) << 16) |
           (uint32_t(uint8_t(b[pos + 2])) << 8) | uint32_t(uint8_t(b[pos + 3]));
}

static void putU32(vector<char>& b, size_t pos, uint32_t v) {
    b[pos] = char(v >> 24); b[pos + 1] = char(v >> 16);
    b[pos + 2] = char(v >> 8); b[pos + 3] = char(v);
}

// Copy of a movie whose sound description holds its 'enda' atom inside a
// 'wave' nested in the 'wave' extension, with the value set to 1
// (little-endian). Only the first 'enda' is moved; the sizes of every atom
// that contains it grow by the 8-byte header of the new 'wave'. The movie
// must keep 'moov' after 'mdat', so no chunk offset changes.
static bool writeWithNestedWaveEnda(const fs::path& src, const fs::path& dst) {
    ifstream in(src, ios::binary);
    if (!in) return false;
    vector<char> bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

    size_t enda = 0;
    for (size_t i = 4; i + 6 <= bytes.size(); i++) {
        if (memcmp(&bytes[i], "enda", 4) == 0) { enda = i - 4; break; }
    }
    if (enda == 0 || getU32(bytes, enda) != 10) return false;

    // Size fields of the atoms that contain 'enda', outermost first
    vector<size_t> parents;
    size_t pos = 0, end = bytes.size();
    bool found = true;
    while (found) {
        found = false;
        while (pos + 8 <= end) {
            const uint32_t size = getU32(bytes, pos);
            if (size < 8 || pos + size > end) return false;
            if (pos < enda && enda < pos + size) {
                const string type(&bytes[pos + 4], 4);
                size_t child = 0;
                if (type == "moov" || type == "trak" || type == "mdia" ||
                    type == "minf" || type == "stbl" || type == "wave") child = pos + 8;
                else if (type == "stsd") child = pos + 16;
                else if (type == "fl32") {
                    // Sound description: extensions follow the fields of its version
                    const int version = (uint8_t(bytes[pos + 16]) << 8) | uint8_t(bytes[pos + 17]);
                    child = pos + (version == 1 ? 52 : version == 2 ? 72 : 36);
                }
                if (child == 0) return false;
                parents.push_back(pos);
                end = pos + size;
                pos = child;
                found = true;
                break;
            }
            pos += size;
        }
    }
    if (parents.empty() || string(&bytes[parents.back() + 4], 4) != "wave") return false;

    for (size_t p : parents) putU32(bytes, p, getU32(bytes, p) + 8);
    const char inner[8] = {0, 0, 0, 18, 'w', 'a', 'v', 'e'};
    bytes.insert(bytes.begin() + enda, inner, inner + 8);
    bytes[enda + 8 + 9] = 1;  // inner 'enda' value: little-endian

    ofstream out(dst, ios::binary);
    out.write(bytes.data(), (streamsize)bytes.size());
    return (bool)out;
}

int main() {
    const fs::path data = fs::path(getDataPath(""));
    printf("data: %s\n", data.string().c_str());

    const Decoded ref = decode(data / "sine_sowt.mov");
    check("sowt: decodes", ref.ok);
    check("sowt: fourcc 'sowt', little-endian 16-bit",
          ref.fourcc == FOURCC_SOWT && !ref.bigEndian && !ref.floatPcm);
    check("sowt: 1 ch, 48000 Hz, 0.5 s",
          ref.buffer.channels == 1 && ref.buffer.sampleRate == 48000 &&
          ref.buffer.numSamples == 24000,
          to_string(ref.buffer.channels) + " ch, " + to_string(ref.buffer.sampleRate) +
          " Hz, " + to_string(ref.buffer.numSamples) + " frames");
    double peak = 0.0, rms = 0.0;
    peakRms(ref.buffer, peak, rms);
    check("sowt: -6 dB sine (peak ~0.5, rms ~0.35)",
          fabs(peak - 0.501) < 0.01 && fabs(rms - 0.354) < 0.01,
          "peak " + to_string(peak) + ", rms " + to_string(rms));
    if (!ref.ok || ref.buffer.samples.empty()) {
        printf("\nFAILED (no reference)\n");
        return 1;
    }

    struct Case {
        const char* file;
        const char* name;
        uint32_t fourcc;
        bool bigEndian;
        bool floatPcm;
        double tolerance;
    };
    const Case cases[] = {
        {"sine_twos.mov", "twos", FOURCC_TWOS, true, false, 0.0},
        {"sine_fl32be.mov", "fl32 (enda 0)", FOURCC_FL32, true, true, 1.0 / 32768.0},
        {"sine_fl32le_enda.mov", "fl32 + enda 1", FOURCC_FL32, false, true, 1.0 / 32768.0},
    };
    for (const Case& c : cases) {
        const Decoded d = decode(data / c.file);
        const string n = c.name;
        check(n + ": decodes", d.ok);
        check(n + ": fourcc and byte order",
              d.fourcc == c.fourcc && d.bigEndian == c.bigEndian && d.floatPcm == c.floatPcm,
              MovParser::fourccToString(d.fourcc) + (d.bigEndian ? " BE" : " LE"));
        const double diff = maxDiff(d.buffer, ref.buffer);
        check(n + ": samples match sowt", diff >= 0.0 && diff <= c.tolerance,
              "max diff " + to_string(diff * 32768.0) + "/32768");
    }

    // 'fl32' without any 'enda' atom: big-endian by default
    const fs::path tmp = fs::temp_directory_path() / "tcxHap_sine_fl32_no_enda.mov";
    int renamed = 0;
    const bool written = writeWithoutEnda(data / "sine_fl32be.mov", tmp, renamed);
    check("fl32 (no enda): test file written", written && renamed == 1,
          to_string(renamed) + " 'enda' atoms renamed");
    if (written) {
        const Decoded d = decode(tmp);
        check("fl32 (no enda): decodes", d.ok);
        check("fl32 (no enda): read as big-endian", d.fourcc == FOURCC_FL32 && d.bigEndian);
        const double diff = maxDiff(d.buffer, ref.buffer);
        check("fl32 (no enda): samples match sowt", diff >= 0.0 && diff <= 1.0 / 32768.0,
              "max diff " + to_string(diff * 32768.0) + "/32768");
        error_code ec;
        fs::remove(tmp, ec);
    }

    // 'fl32' whose 'enda' (set to 1) sits in a 'wave' inside 'wave': 'wave'
    // is read one level deep, so that 'enda' is not used and the samples
    // (big-endian in this file) are read as big-endian, the 'fl32' default
    const fs::path nested = fs::temp_directory_path() / "tcxHap_sine_fl32_nested_wave.mov";
    const bool nestedWritten = writeWithNestedWaveEnda(data / "sine_fl32be.mov", nested);
    check("fl32 (enda in a nested wave): test file written", nestedWritten);
    if (nestedWritten) {
        const Decoded d = decode(nested);
        check("fl32 (enda in a nested wave): decodes", d.ok);
        check("fl32 (enda in a nested wave): read as big-endian",
              d.fourcc == FOURCC_FL32 && d.bigEndian);
        const double diff = maxDiff(d.buffer, ref.buffer);
        check("fl32 (enda in a nested wave): samples match sowt",
              diff >= 0.0 && diff <= 1.0 / 32768.0,
              "max diff " + to_string(diff * 32768.0) + "/32768");
        error_code ec;
        fs::remove(nested, ec);
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
