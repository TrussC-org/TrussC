// =============================================================================
// tcxHap tests — PCM audio byte order (#419), sample table checks (#343)
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
// Sample table checks (#343) parse copies of sine_sowt.mov with changed table
// counts, sizes and atom sizes, and a copy cut inside 'mdat', all made at run
// time in the temp folder.
// Headless: no window, no GPU, no audio device.
// =============================================================================

#include <TrussC.h>
#include "tcxHapPlayer.h"

#include <chrono>
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

// -----------------------------------------------------------------------------
// Sample table checks (#343)
// -----------------------------------------------------------------------------

static vector<char> readBytes(const fs::path& path) {
    ifstream in(path, ios::binary);
    if (!in) return {};
    return vector<char>((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

static bool writeBytes(const fs::path& path, const vector<char>& bytes) {
    ofstream out(path, ios::binary);
    out.write(bytes.data(), (streamsize)bytes.size());
    return (bool)out;
}

static bool isType(const vector<char>& b, size_t pos, const char* type) {
    return memcmp(&b[pos + 4], type, 4) == 0;
}

// Offset of the first child atom of the given type in [pos, end), or 0
static size_t findChild(const vector<char>& b, size_t pos, size_t end, const char* type) {
    while (pos + 8 <= end) {
        const uint32_t size = getU32(b, pos);
        if (size < 8 || pos + size > end) return 0;
        if (isType(b, pos, type)) return pos;
        pos += size;
    }
    return 0;
}

// Offset of the 'trak' whose 'hdlr' has the given handler type, or 0
static size_t findTrak(const vector<char>& b, const char* handler) {
    const size_t moov = findChild(b, 0, b.size(), "moov");
    if (!moov) return 0;
    const size_t moovEnd = moov + getU32(b, moov);
    size_t pos = moov + 8;
    while (pos + 8 <= moovEnd) {
        const uint32_t size = getU32(b, pos);
        if (size < 8 || pos + size > moovEnd) return 0;
        if (isType(b, pos, "trak")) {
            const size_t mdia = findChild(b, pos + 8, pos + size, "mdia");
            const size_t hdlr = mdia ? findChild(b, mdia + 8, mdia + getU32(b, mdia), "hdlr") : 0;
            if (hdlr && memcmp(&b[hdlr + 16], handler, 4) == 0) return pos;
        }
        pos += size;
    }
    return 0;
}

// Offset of a table atom ('stsz', 'stts', ...) in the 'stbl' of a track, or 0
static size_t findTable(const vector<char>& b, const char* handler, const char* type) {
    size_t pos = findTrak(b, handler);
    for (const char* t : {"mdia", "minf", "stbl"}) {
        if (!pos) return 0;
        pos = findChild(b, pos + 8, pos + getU32(b, pos), t);
    }
    return pos ? findChild(b, pos + 8, pos + getU32(b, pos), type) : 0;
}

// Copy of a movie whose constant-size video 'stsz' is written as a
// variable-size table with the same size for every sample. The atoms that
// contain it grow by 4 bytes per sample. 'moov' must come after 'mdat', so
// no chunk offset changes. Returns an empty vector when that layout is not
// found.
static vector<char> withVariableVideoStsz(const vector<char>& src) {
    vector<char> b = src;
    const size_t moov = findChild(b, 0, b.size(), "moov");
    const size_t mdat = findChild(b, 0, b.size(), "mdat");
    const size_t trak = findTrak(b, "vide");
    if (!moov || !mdat || moov < mdat || !trak) return {};
    vector<size_t> parents = {moov, trak};
    size_t pos = trak;
    for (const char* t : {"mdia", "minf", "stbl"}) {
        pos = findChild(b, pos + 8, pos + getU32(b, pos), t);
        if (!pos) return {};
        parents.push_back(pos);
    }
    const size_t stsz = findChild(b, pos + 8, pos + getU32(b, pos), "stsz");
    if (!stsz || getU32(b, stsz) != 20) return {};
    const uint32_t size = getU32(b, stsz + 12);
    const uint32_t count = getU32(b, stsz + 16);
    if (size == 0 || count == 0 || count > 1000) return {};

    const uint32_t grow = 4 * count;
    for (size_t p : parents) putU32(b, p, getU32(b, p) + grow);
    putU32(b, stsz, 20 + grow);
    putU32(b, stsz + 12, 0);
    vector<char> entries(grow);
    for (uint32_t i = 0; i < count; i++) putU32(entries, 4 * i, size);
    b.insert(b.begin() + stsz + 20, entries.begin(), entries.end());
    return b;
}

// Copy of a movie with 'moov' moved in front of 'mdat' (the 'stco' chunk
// offsets shifted by the size of 'moov'), cut off cutInMdat bytes into the
// 'mdat' payload
static bool writeMoovFirstTruncated(const vector<char>& src, const fs::path& dst,
                                    size_t cutInMdat) {
    vector<char> b = src;
    const size_t mdat = findChild(b, 0, b.size(), "mdat");
    const size_t moov = findChild(b, 0, b.size(), "moov");
    if (!mdat || !moov || moov < mdat || moov + getU32(b, moov) != b.size()) return false;
    const uint32_t moovSize = getU32(b, moov);
    for (const char* handler : {"vide", "soun"}) {
        const size_t stco = findTable(b, handler, "stco");
        if (!stco) return false;
        const uint32_t count = getU32(b, stco + 12);
        for (uint32_t i = 0; i < count; i++) {
            const size_t at = stco + 16 + 4 * i;
            putU32(b, at, getU32(b, at) + moovSize);
        }
    }
    vector<char> out(b.begin(), b.begin() + mdat);
    out.insert(out.end(), b.begin() + moov, b.end());
    const size_t keep = min(moov - mdat, 8 + cutInMdat);
    out.insert(out.end(), b.begin() + mdat, b.begin() + mdat + keep);
    return writeBytes(dst, out);
}

struct Parsed {
    bool opened = false;
    double ms = 0.0;
    bool hasVideo = false, hasAudio = false;
    size_t videoSamples = 0, audioSamples = 0;
};

static Parsed parseFile(const fs::path& path) {
    Parsed r;
    MovParser parser;
    const auto t0 = chrono::steady_clock::now();
    r.opened = parser.open(path);
    r.ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    if (const MovTrack* v = parser.getInfo().getVideoTrack()) {
        r.hasVideo = true;
        r.videoSamples = v->samples.size();
    }
    if (const MovTrack* a = parser.getInfo().getAudioTrack()) {
        r.hasAudio = true;
        r.audioSamples = a->samples.size();
    }
    return r;
}

static string describe(const Parsed& p) {
    return string(p.opened ? "opened" : "not opened") + ", " + to_string(p.ms) + " ms, video " +
           (p.hasVideo ? to_string(p.videoSamples) : string("none")) + ", audio " +
           (p.hasAudio ? to_string(p.audioSamples) : string("none"));
}

// Upper bound for open() on the changed copies; the valid file parses in
// well under a millisecond
static constexpr double kQuickMs = 2000.0;

static void sampleTableTests(const fs::path& data) {
    const vector<char> orig = readBytes(data / "sine_sowt.mov");
    check("tables: sine_sowt.mov read", !orig.empty());
    if (orig.empty()) return;

    // Video track: 5 HAP frames of 115 bytes; audio track: 24000 PCM frames
    // of 2 bytes. Both 'stsz' tables are constant-size.
    const Parsed valid = parseFile(data / "sine_sowt.mov");
    check("tables: valid file has 5 video and 24000 audio samples",
          valid.opened && valid.videoSamples == 5 && valid.audioSamples == 24000, describe(valid));

    MovParser ref;
    ref.open(data / "sine_sowt.mov");
    const MovTrack* refVideo = ref.getInfo().getVideoTrack();

    const size_t videoStts = findTable(orig, "vide", "stts");
    const size_t audioStsz = findTable(orig, "soun", "stsz");
    const size_t mdat = findChild(orig, 0, orig.size(), "mdat");
    check("tables: atoms found in sine_sowt.mov",
          refVideo && mdat && videoStts && audioStsz && getU32(orig, audioStsz + 12) == 2);
    if (!refVideo || !mdat || !videoStts || !audioStsz) return;

    const fs::path tmp = fs::temp_directory_path() / "tcxHap_sample_tables.mov";

    // Copy with the video 'stsz' written as a variable-size table (one entry
    // per frame, same sizes); it parses to the same video samples
    const vector<char> var = withVariableVideoStsz(orig);
    const size_t videoStsz = var.empty() ? 0 : findTable(var, "vide", "stsz");
    bool varSame = false;
    if (videoStsz && writeBytes(tmp, var)) {
        MovParser parser;
        parser.open(tmp);
        const MovTrack* v = parser.getInfo().getVideoTrack();
        varSame = v && v->samples.size() == refVideo->samples.size();
        for (size_t i = 0; varSame && i < v->samples.size(); i++) {
            varSame = v->samples[i].offset == refVideo->samples[i].offset &&
                      v->samples[i].size == refVideo->samples[i].size;
        }
    }
    check("tables: variable-size video stsz copy parses to the same samples", varSame);
    if (!varSame) return;

    // Video 'stsz' entry count larger than the atom: the video track is skipped
    {
        vector<char> b = var;
        putU32(b, videoStsz + 16, 0xFFFFFFF0u);
        writeBytes(tmp, b);
        const Parsed p = parseFile(tmp);
        check("tables: video stsz count 0xFFFFFFF0 -> no video track, audio kept",
              p.ms < kQuickMs && !p.hasVideo && p.hasAudio && p.audioSamples == 24000,
              describe(p));
    }

    // Video 'stts' entry count larger than the atom: 'stts' is ignored and
    // the video track is unchanged
    {
        vector<char> b = orig;
        putU32(b, videoStts + 12, 0xFFFFFFF0u);
        writeBytes(tmp, b);
        MovParser parser;
        const auto t0 = chrono::steady_clock::now();
        parser.open(tmp);
        const double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        const MovTrack* v = parser.getInfo().getVideoTrack();
        bool same = v && v->samples.size() == refVideo->samples.size();
        for (size_t i = 0; same && i < v->samples.size(); i++) {
            same = v->samples[i].offset == refVideo->samples[i].offset &&
                   v->samples[i].size == refVideo->samples[i].size &&
                   v->samples[i].timestamp == refVideo->samples[i].timestamp;
        }
        check("tables: video stts count 0xFFFFFFF0 -> video track unchanged",
              ms < kQuickMs && same, to_string(ms) + " ms");
    }

    // Audio constant-size 'stsz' with a count of 0xFFFFFFF0: the sample count
    // is what 'stsc' / 'stco' place (24000)
    {
        vector<char> b = orig;
        putU32(b, audioStsz + 16, 0xFFFFFFF0u);
        writeBytes(tmp, b);
        const Parsed p = parseFile(tmp);
        check("tables: audio constant stsz count 0xFFFFFFF0 -> 24000 audio samples",
              p.ms < kQuickMs && p.videoSamples == 5 && p.audioSamples == 24000, describe(p));
    }

    // Constant-size 'stsz' with a count of 0xFFFFFFF0, every 'stsc' entry
    // with 0xFFFFFFF0 samples per chunk, and the first chunk past the end of
    // the file: the sample count is the 'stts' total (24000)
    {
        vector<char> b = orig;
        putU32(b, audioStsz + 16, 0xFFFFFFF0u);
        const size_t stsc = findTable(b, "soun", "stsc");
        const size_t stco = findTable(b, "soun", "stco");
        const bool patched = stsc && stco && getU32(b, stsc + 12) > 0 && getU32(b, stco + 12) > 0;
        if (patched) {
            const uint32_t entries = getU32(b, stsc + 12);
            for (uint32_t i = 0; i < entries; i++) putU32(b, stsc + 16 + 12 * i + 4, 0xFFFFFFF0u);
            putU32(b, stco + 16, uint32_t(b.size() + 4096));
        }
        writeBytes(tmp, b);
        const Parsed p = parseFile(tmp);
        check("tables: constant stsz + stsc 0xFFFFFFF0 per chunk, chunk past the end -> stts total (24000)",
              patched && p.ms < kQuickMs && p.audioSamples == 24000, describe(p));
    }

    // A child atom larger than its parent: the video track's 'mdia' size
    // made larger than its 'trak'. open() returns and skips the video track.
    {
        vector<char> b = orig;
        const size_t trak = findTrak(b, "vide");
        const size_t mdia = trak ? findChild(b, trak + 8, trak + getU32(b, trak), "mdia") : 0;
        if (mdia) putU32(b, mdia, getU32(b, mdia) + 0x10000);
        writeBytes(tmp, b);
        const Parsed p = parseFile(tmp);
        check("tables: child atom larger than its parent -> video track skipped",
              mdia && p.ms < kQuickMs && !p.hasVideo && p.hasAudio, describe(p));
    }

    // One variable 'stsz' entry of 0xFFFFFFF0: that sample lies past the end
    // of the file, so readSample() returns false without sizing the buffer
    {
        vector<char> b = var;
        putU32(b, videoStsz + 20 + 4 * 2, 0xFFFFFFF0u);
        writeBytes(tmp, b);
        MovParser parser;
        const bool opened = parser.open(tmp);
        const MovTrack* v = parser.getInfo().getVideoTrack();
        check("tables: stsz entry 0xFFFFFFF0 -> video track kept with 5 samples",
              opened && v && v->samples.size() == 5);
        if (v && v->samples.size() == 5) {
            vector<uint8_t> buf;
            const bool read = parser.readSample(*v, 2, buf);
            check("tables: stsz entry 0xFFFFFFF0 -> readSample() false, buffer not sized",
                  !read && buf.capacity() == 0, "capacity " + to_string(buf.capacity()));
            check("tables: stsz entry 0xFFFFFFF0 -> next sample reads",
                  parser.readSample(*v, 3, buf) && buf.size() == v->samples[3].size);
        }
    }

    // 'moov' in front of 'mdat', file cut inside 'mdat' where the third video
    // frame starts: the sample counts are unchanged, a sample past the cut
    // fails to read, and earlier samples still read after that
    {
        const size_t cut = (size_t)(refVideo->samples[2].offset - (mdat + 8));
        const bool written = writeMoovFirstTruncated(orig, tmp, cut);
        check("tables: moov-first copy cut inside mdat written", written);
        if (written) {
            MovParser parser;
            const bool opened = parser.open(tmp);
            const MovTrack* v = parser.getInfo().getVideoTrack();
            const MovTrack* a = parser.getInfo().getAudioTrack();
            check("tables: cut file -> sample counts unchanged (5 video, 24000 audio)",
                  opened && v && a && v->samples.size() == 5 && a->samples.size() == 24000);
            if (v && v->samples.size() == 5) {
                vector<uint8_t> buf;
                const bool last = parser.readSample(*v, 4, buf);
                const bool first = parser.readSample(*v, 0, buf);
                const bool firstSize = buf.size() == v->samples[0].size;
                check("tables: cut file -> sample past the cut fails, then sample 0 reads",
                      !last && first && firstSize);
                check("tables: cut file -> sample 1 reads too",
                      parser.readSample(*v, 1, buf) && buf.size() == v->samples[1].size);
            }
        }
    }

    error_code ec;
    fs::remove(tmp, ec);
}

int runVideoTests();

int main() {
    g_fail += runVideoTests();
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

    sampleTableTests(data);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
