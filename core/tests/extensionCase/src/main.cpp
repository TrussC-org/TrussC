// =============================================================================
// extensionCase — loaders and savers match the file extension case-insensitively
// (#305), and use the path as given.
//
// - Sound::load() picks its decoder whatever the case of the extension
//   (.Wav, .wAV, .Mp3, .OgG, .Flac, .M4a, .Aac), like SoundBuffer::load() and
//   loadStream() already did. Before #305 it kept its own dispatch that took
//   only all-lower or all-upper extensions, so ".Wav" failed with
//   UnsupportedFormat. A file of garbage under a mixed-case audio extension
//   (a missing file for AAC) must reach its loader and fail there, not at
//   the dispatch.
// - Pixels::save() picks the encoder whatever the case (.Jpg, .jPeG, .Bmp,
//   .PnG). Before #305 only all-lower and all-upper matched, and a ".Jpg"
//   path got a PNG written into it. The file is checked by its magic bytes.
// - Only the comparison ignores case: a save to "s6.Jpg" creates exactly
//   "s6.Jpg", and on a case-sensitive file system a.wav and a.WAV coexist and
//   each loads by its own name (skipped, and said so, on a case-insensitive
//   one).
//
// Not covered here: the per-platform screenshot savers need a framebuffer,
// and the hot reload watcher is checked by hotReloadLifecycle (it only
// exists in a hot reload build).
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define TC_GETPID _getpid
#else
#include <unistd.h>
#define TC_GETPID getpid
#endif

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

static const char* errorName(LoadError e) {
    switch (e) {
        case LoadError::None:              return "None";
        case LoadError::FileNotFound:      return "FileNotFound";
        case LoadError::UnsupportedFormat: return "UnsupportedFormat";
        case LoadError::DecodeFailed:      return "DecodeFailed";
        case LoadError::Unknown:           return "Unknown";
    }
    return "?";
}

static void put16(ofstream& f, uint16_t v) {
    char b[2] = {(char)(v & 0xff), (char)(v >> 8)};
    f.write(b, 2);
}
static void put32(ofstream& f, uint32_t v) {
    char b[4] = {(char)(v & 0xff), (char)((v >> 8) & 0xff), (char)((v >> 16) & 0xff),
                 (char)(v >> 24)};
    f.write(b, 4);
}

// 16-bit mono PCM WAV, 44.1 kHz, `frames` samples of a quiet ramp.
static void writeWav(const fs::path& p, uint32_t frames) {
    ofstream f(p, ios::binary);
    const uint32_t dataBytes = frames * 2;
    f.write("RIFF", 4); put32(f, 36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, 44100); put32(f, 44100 * 2); put16(f, 2); put16(f, 16);
    f.write("data", 4); put32(f, dataBytes);
    for (uint32_t i = 0; i < frames; ++i) put16(f, (uint16_t)(int16_t)(i % 256));
}

static void writeGarbage(const fs::path& p) {
    ofstream f(p, ios::binary);
    string junk(4096, '\0');
    for (size_t i = 0; i < junk.size(); ++i) junk[i] = (char)((i * 131 + 7) & 0xff);
    f.write(junk.data(), (streamsize)junk.size());
}

static vector<unsigned char> head(const fs::path& p, size_t n) {
    ifstream f(p, ios::binary);
    vector<unsigned char> b(n, 0);
    if (f) f.read(reinterpret_cast<char*>(b.data()), (streamsize)n);
    return b;
}

// The directory entry names exactly as the file system lists them.
static bool listsName(const fs::path& dir, const string& name) {
    for (auto& e : fs::directory_iterator(dir)) {
        if (internal::pathToUtf8(e.path().filename()) == name) return true;
    }
    return false;
}

} // namespace

TC_CORE_TEST_MAIN() {
    const fs::path dir = fs::temp_directory_path() /
                         ("tc_extensionCase_" + to_string((long long)TC_GETPID()));
    error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // A case-sensitive file system keeps a.wav and a.WAV apart.
    writeWav(dir / "probe.wav", 16);
    const bool caseSensitiveFs = !fs::exists(dir / "probe.WAV");
    fs::remove(dir / "probe.wav", ec);
    printf("-- file system is case-%s --\n", caseSensitiveFs ? "sensitive" : "insensitive");

    // -------------------------------------------------------------------------
    // Sound::load(): the decoder is picked whatever the case
    // -------------------------------------------------------------------------
    for (const char* name : {"tone.wav", "tone2.WAV", "tone3.Wav", "tone4.wAV"}) {
        writeWav(dir / name, 441);
        Sound s;
        LoadResult r = s.load(dir / name);
        check(string("Sound::load(\"") + name + "\") loads", (bool)r,
              string(errorName(r.error)) + ": " + r.message);
    }

    // SoundBuffer::load() and loadStream() already ignored case; they and
    // Sound::load() must agree.
    {
        SoundBuffer b;
        LoadResult r = b.load(dir / "tone3.Wav");
        check("SoundBuffer::load(\"tone3.Wav\") loads", (bool)r, r.message);
        Sound s;
        LoadResult rs = s.loadStream(dir / "tone3.Wav");
        check("Sound::loadStream(\"tone3.Wav\") loads", (bool)rs, rs.message);
    }

    // Garbage under a mixed-case audio extension must reach its decoder and
    // fail there, not at the extension dispatch ("unsupported extension").
    // Checked by the message: a decoder may itself answer UnsupportedFormat
    // (AAC on Android).
    for (const char* name : {"junk.Mp3", "junk.OgG", "junk.Flac", "junk.fLAC", "junk.Ogg"}) {
        writeGarbage(dir / name);
        Sound s;
        LoadResult r = s.load(dir / name);
        check(string("Sound::load(\"") + name + "\") reaches the decoder",
              !r && r.message.find("unsupported extension") == string::npos,
              string(errorName(r.error)) + ": " + r.message);
    }
    // AAC goes to the platform decoder, which checks that the file exists
    // first; a missing file keeps GStreamer out of it (fed garbage, its
    // Linux decode never returns).
    for (const char* name : {"missing.M4a", "missing.Aac", "missing.aAC"}) {
        Sound s;
        LoadResult r = s.load(dir / name);
        check(string("Sound::load(\"") + name + "\") reaches the AAC loader",
              !r && r.message.find("unsupported extension") == string::npos,
              string(errorName(r.error)) + ": " + r.message);
    }

    // An extension that is not audio at all is still refused, by name.
    {
        writeGarbage(dir / "junk.Txt");
        Sound s;
        LoadResult r = s.load(dir / "junk.Txt");
        check("Sound::load(\"junk.Txt\") is UnsupportedFormat",
              r.error == LoadError::UnsupportedFormat, errorName(r.error));
        check("the failure is the extension dispatch's",
              r.message.find("unsupported extension") != string::npos, r.message);
        check("the failure names the file", r.message.find("junk.Txt") != string::npos,
              r.message);
    }

    // Only the comparison ignores case: a.wav and a.WAV are two files.
    if (caseSensitiveFs) {
        writeWav(dir / "a.wav", 4410);    // 0.1 s
        writeWav(dir / "a.WAV", 22050);   // 0.5 s
        Sound lower, upper;
        bool ok = lower.load(dir / "a.wav") && upper.load(dir / "a.WAV");
        check("a.wav and a.WAV both load", ok);
        check("a.wav loads its own file (0.1 s)", fabs(lower.getDuration() - 0.1f) < 0.01f,
              to_string(lower.getDuration()));
        check("a.WAV loads its own file (0.5 s)", fabs(upper.getDuration() - 0.5f) < 0.01f,
              to_string(upper.getDuration()));
    } else {
        printf("%-72s SKIP\n", "a.wav / a.WAV coexist (case-insensitive file system)");
    }

    // -------------------------------------------------------------------------
    // Pixels::save(): the encoder is picked whatever the case
    // -------------------------------------------------------------------------
    Pixels px;
    px.allocate(8, 8, 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) px.setColor(x, y, Color(x / 7.0f, y / 7.0f, 0.5f, 1.0f));

    auto isPng  = [](const vector<unsigned char>& b) {
        return b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G';
    };
    auto isJpeg = [](const vector<unsigned char>& b) {
        return b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF;
    };
    auto isBmp  = [](const vector<unsigned char>& b) { return b[0] == 'B' && b[1] == 'M'; };

    struct SaveCase { const char* name; int kind; };   // 0 png, 1 jpeg, 2 bmp
    const SaveCase saves[] = {
        {"s1.png", 0}, {"s2.PNG", 0}, {"s3.PnG", 0},
        {"s4.jpg", 1}, {"s5.JPG", 1}, {"s6.Jpg", 1}, {"s7.jPeG", 1}, {"s8.Jpeg", 1},
        {"s9.bmp", 2}, {"s10.BMP", 2}, {"s11.Bmp", 2},
    };
    for (auto& c : saves) {
        const fs::path p = dir / c.name;
        bool saved = px.save(p);
        auto b = head(p, 4);
        bool right = c.kind == 0 ? isPng(b) : c.kind == 1 ? isJpeg(b) : isBmp(b);
        const char* want = c.kind == 0 ? "PNG" : c.kind == 1 ? "JPEG" : "BMP";
        check(string("Pixels::save(\"") + c.name + "\") writes " + want, saved && right);
    }

    // The file is written under the path as given (a case-preserving file
    // system lists the name as created, case-sensitive or not).
    check("save(\"s6.Jpg\") created \"s6.Jpg\"", listsName(dir, "s6.Jpg"));
    check("save(\"s6.Jpg\") created no \"s6.jpg\"", !listsName(dir, "s6.jpg"));

    fs::remove_all(dir, ec);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
