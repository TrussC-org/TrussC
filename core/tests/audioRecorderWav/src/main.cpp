// =============================================================================
// core/tests/audioRecorderWav — behavioral regression test for #336: the WAV
// header AudioRecorder writes never wraps past 4 GiB of samples.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The header part works on the header alone (internal::wavSizeFields(),
// writeWavHeader() and patchWavHeader() on a memory stream), so no 4 GiB
// file is written. The recording part runs the real AudioEngine on
// miniaudio's null backend (internal::setNullAudioBackendForTests()), a
// device-less clock that still drives the mixer callback.
//
// Guards the invariants:
//   - Every header reserves a 36-byte JUNK chunk right after "WAVE": the
//     samples start at byte 80 (S16) or 92 (F32); the data chunk header is
//     at 72 / 84.
//   - The size fields are computed in 64 bits. A take stays plain RIFF with
//     exact 32-bit fields up to the last frame whose RIFF size (file size - 8)
//     fits 32 bits, and becomes RF64 from the next frame on. That includes
//     the gap where the data size still fits 32 bits and only the RIFF size
//     does not (72 bytes below 2^32 for S16, 84 for F32).
//   - An RF64 patch rewrites "RIFF" to "RF64", turns JUNK into ds64 with the
//     64-bit RIFF size, data size and sample count (EBU Tech 3306), sets the
//     32-bit RIFF, data and fact fields to 0xFFFFFFFF, and leaves the rest of
//     the header alone.
//   - A short S16 and F32 take on the null backend is a plain RIFF file with
//     the JUNK chunk, its sizes match the frames written, and it loads through
//     SoundBuffer with numSamples == getRecordedSeconds() * sampleRate. No
//     RF64 notice is logged for it.
//   - (Linux) A take whose file writes fail (recorded into /dev/full) logs an
//     error on stop() and neither an RF64 notice nor the "stopped" notice:
//     the frame count says what was handed to the stream, not what reached
//     the file.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// Wait (up to `timeoutMs`) until `pred()` holds.
template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 1) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return pred();
}

// --- Reading a header ---------------------------------------------------------
// The writer stores fields in host order, which is little-endian on every
// supported platform; read them back the same way.
static string tagAt(const string& b, size_t pos) {
    return pos + 4 <= b.size() ? b.substr(pos, 4) : string();
}
static uint32_t u32At(const string& b, size_t pos) {
    uint32_t v = 0;
    if (pos + 4 <= b.size()) memcpy(&v, b.data() + pos, 4);
    return v;
}
static uint64_t u64At(const string& b, size_t pos) {
    uint64_t v = 0;
    if (pos + 8 <= b.size()) memcpy(&v, b.data() + pos, 8);
    return v;
}
static uint16_t u16At(const string& b, size_t pos) {
    uint16_t v = 0;
    if (pos + 2 <= b.size()) memcpy(&v, b.data() + pos, 2);
    return v;
}
static string readFile(const fs::path& p) {
    ifstream f(p, ios::binary);
    return string(istreambuf_iterator<char>(f), istreambuf_iterator<char>());
}

static string u64s(uint64_t v) { return to_string(v); }

static constexpr uint64_t kMax32 = 0xFFFFFFFFull;

// --- Size fields: plain RIFF up to the limit, RF64 past it -------------------
static void checkSizeFields() {
    using internal::wavSizeFields;

    // Header sizes with the JUNK chunk: RIFF size = data size + header - 8.
    const uint64_t hdrS16 = 80, hdrF32 = 92;

    // A short take: plain, every field exact.
    {
        auto f = wavSizeFields(48000, 2, 2, hdrS16);
        check("1 s of S16 stereo: plain RIFF, exact 32-bit fields",
              !f.rf64 && f.dataSize == 192000 && f.dataSize32 == 192000
              && f.riffSize == 192072 && f.riffSize32 == 192072 && f.factCount32 == 48000);
    }

    // S16 stereo (4 bytes a frame): the last frame whose RIFF size fits.
    {
        auto below = wavSizeFields(1073741805ull, 2, 2, hdrS16);
        check("S16 stereo, RIFF size 2^32 - 4: still plain RIFF",
              !below.rf64 && below.dataSize == 4294967220ull && below.riffSize == 4294967292ull
              && below.dataSize32 == 4294967220u && below.riffSize32 == 4294967292u,
              u64s(below.riffSize));
        // One frame more: the data size still fits 32 bits, the RIFF size doesn't.
        auto gap = wavSizeFields(1073741806ull, 2, 2, hdrS16);
        check("S16 stereo, data fits 32 bits but RIFF size doesn't: RF64",
              gap.rf64 && gap.dataSize == 4294967224ull && gap.dataSize <= kMax32
              && gap.riffSize == 4294967296ull
              && gap.riffSize32 == 0xFFFFFFFFu && gap.dataSize32 == 0xFFFFFFFFu
              && gap.factCount32 == 0xFFFFFFFFu && gap.sampleCount == 1073741806ull,
              u64s(gap.riffSize));
        // Data of exactly 2^32 bytes, and far above.
        auto at = wavSizeFields(1ull << 30, 2, 2, hdrS16);
        check("S16 stereo, data of exactly 2^32 bytes: RF64 with 64-bit sizes",
              at.rf64 && at.dataSize == (1ull << 32) && at.riffSize == (1ull << 32) + 72,
              u64s(at.dataSize));
        auto above = wavSizeFields(1ull << 31, 2, 2, hdrS16);
        check("S16 stereo, 8 GiB of data: RF64, sizes don't wrap",
              above.rf64 && above.dataSize == (1ull << 33) && above.riffSize == (1ull << 33) + 72
              && above.sampleCount == (1ull << 31),
              u64s(above.dataSize));
    }

    // F32 stereo (8 bytes a frame), header with the fact chunk.
    {
        auto below = wavSizeFields(536870901ull, 2, 4, hdrF32);
        check("F32 stereo, RIFF size 2^32 - 4: still plain RIFF",
              !below.rf64 && below.dataSize == 4294967208ull && below.riffSize == 4294967292ull
              && below.riffSize32 == 4294967292u && below.factCount32 == 536870901u,
              u64s(below.riffSize));
        auto gap = wavSizeFields(536870902ull, 2, 4, hdrF32);
        check("F32 stereo, data fits 32 bits but RIFF size doesn't: RF64",
              gap.rf64 && gap.dataSize == 4294967216ull && gap.dataSize <= kMax32
              && gap.riffSize == 4294967300ull && gap.factCount32 == 0xFFFFFFFFu,
              u64s(gap.riffSize));
        // The issue's example: 3.5 h of 48 kHz stereo F32 (about 4.5 GiB).
        auto take = wavSizeFields(604800000ull, 2, 4, hdrF32);
        check("F32 stereo, 3.5 h at 48 kHz: RF64 with the full 64-bit sizes",
              take.rf64 && take.dataSize == 4838400000ull && take.riffSize == 4838400084ull
              && take.sampleCount == 604800000ull,
              u64s(take.dataSize));
    }

    // The switch sits exactly where the RIFF size passes 32 bits, for other
    // channel counts too.
    {
        bool ok = true;
        string detail;
        for (int bps : {2, 4}) {
            const uint64_t hdr = bps == 2 ? hdrS16 : hdrF32;
            for (int ch : {1, 2, 6, 8}) {
                const uint64_t frameBytes = (uint64_t)ch * bps;
                const uint64_t last = (kMax32 - (hdr - 8)) / frameBytes;
                auto a = wavSizeFields(last, ch, bps, hdr);
                auto b = wavSizeFields(last + 1, ch, bps, hdr);
                const bool good = !a.rf64 && a.riffSize <= kMax32 && a.riffSize32 == a.riffSize
                               && b.rf64 && b.riffSize > kMax32
                               && b.riffSize == hdr - 8 + (last + 1) * frameBytes;
                if (!good) {
                    ok = false;
                    detail += to_string(ch) + "ch/" + to_string(bps * 8) + "bit ";
                }
            }
        }
        check("RF64 from the first frame that doesn't fit, 1/2/6/8 ch", ok, detail);
    }
}

// --- The header bytes: written, and patched as RIFF or RF64 -------------------
static void checkHeaderBytes() {
    // S16: layout, then a plain patch.
    {
        ostringstream out(ios::binary);
        auto l = internal::writeWavHeader(out, 48000, 2, false);
        const string b = out.str();
        check("S16 header: 80 bytes, JUNK (28) right after WAVE",
              b.size() == 80 && l.dataStart == 80 && tagAt(b, 0) == "RIFF" && tagAt(b, 8) == "WAVE"
              && l.junkPos == 12 && tagAt(b, 12) == "JUNK" && u32At(b, 16) == 28
              && b.substr(20, 28) == string(28, '\0'),
              to_string(b.size()) + " bytes");
        check("S16 header: fmt at 48, data size at 76, no fact",
              tagAt(b, 48) == "fmt " && u16At(b, 56) == 1 && tagAt(b, 72) == "data"
              && l.dataSizePos == 76 && l.factPos == 0);

        auto f = internal::patchWavHeader(out, l, 1000, 2, false);
        const string p = out.str();
        check("S16 patch below the limit: plain RIFF, JUNK kept",
              !f.rf64 && p.size() == 80 && tagAt(p, 0) == "RIFF" && u32At(p, 4) == 72 + 4000
              && tagAt(p, 12) == "JUNK" && p.substr(20, 28) == string(28, '\0')
              && u32At(p, 76) == 4000,
              to_string(u32At(p, 4)));
    }

    // F32: layout, then an RF64 patch for the 3.5 h take.
    {
        ostringstream out(ios::binary);
        auto l = internal::writeWavHeader(out, 48000, 2, true);
        const string b = out.str();
        check("F32 header: 92 bytes, JUNK, fmt, fact at 72, data at 84",
              b.size() == 92 && l.dataStart == 92 && tagAt(b, 12) == "JUNK"
              && tagAt(b, 48) == "fmt " && u16At(b, 56) == 3
              && tagAt(b, 72) == "fact" && l.factPos == 80
              && tagAt(b, 84) == "data" && l.dataSizePos == 88,
              to_string(b.size()) + " bytes");

        auto f = internal::patchWavHeader(out, l, 604800000ull, 2, true);
        const string p = out.str();
        check("F32 patch past 4 GiB: RF64 and a ds64 chunk in place of JUNK",
              f.rf64 && p.size() == 92 && tagAt(p, 0) == "RF64" && tagAt(p, 8) == "WAVE"
              && tagAt(p, 12) == "ds64" && u32At(p, 16) == 28);
        check("... ds64 holds the 64-bit RIFF size, data size and sample count",
              u64At(p, 20) == 4838400084ull && u64At(p, 28) == 4838400000ull
              && u64At(p, 36) == 604800000ull && u32At(p, 44) == 0,
              u64s(u64At(p, 20)) + " / " + u64s(u64At(p, 28)) + " / " + u64s(u64At(p, 36)));
        check("... the 32-bit RIFF, fact and data fields are 0xFFFFFFFF",
              u32At(p, 4) == 0xFFFFFFFFu && u32At(p, 80) == 0xFFFFFFFFu
              && u32At(p, 88) == 0xFFFFFFFFu);
        check("... the fmt chunk is untouched",
              p.substr(48, 24) == b.substr(48, 24) && tagAt(p, 72) == "fact" && tagAt(p, 84) == "data");
    }
}

// --- A short take on the null backend -----------------------------------------

static mutex g_logMutex;
static vector<string> g_notices;
static vector<string> g_errors;

static size_t countIn(const vector<string>& list, const string& needle) {
    lock_guard<mutex> lock(g_logMutex);
    size_t n = 0;
    for (auto& m : list) if (m.find(needle) != string::npos) ++n;
    return n;
}
static size_t countNotices(const string& needle) { return countIn(g_notices, needle); }
static size_t countErrors(const string& needle) { return countIn(g_errors, needle); }

static void recordTake(AudioRecordSettings::SampleFormat format, int sampleRate) {
    const bool isF32 = format == AudioRecordSettings::SampleFormat::F32;
    const string name = isF32 ? "F32" : "S16";
    const int bps = isF32 ? 4 : 2;
    const size_t dataTag = isF32 ? 84 : 72;
    const uint64_t dataStart = isF32 ? 92 : 80;

    const fs::path wav = fs::temp_directory_path()
        / (string("tc_audioRecorderWav_") + (isF32 ? "f32" : "s16") + ".wav");
    AudioRecordSettings rs;
    rs.format = format;
    AudioRecorder rec;   // engine 2 ch -> stereo file
    const bool started = rec.start(wav, rs);
    const bool some = started && waitFor([&] { return rec.getRecordedSeconds() >= 0.25; }, 5000);
    rec.stop();
    check(name + " take records on the null backend", started && some);

    const uint64_t frames = (uint64_t)llround(rec.getRecordedSeconds() * sampleRate);
    const string b = readFile(wav);
    const uint64_t dataBytes = frames * 2 * bps;
    check(name + " take: plain RIFF with the JUNK chunk after WAVE",
          tagAt(b, 0) == "RIFF" && tagAt(b, 8) == "WAVE"
          && tagAt(b, 12) == "JUNK" && u32At(b, 16) == 28 && tagAt(b, 48) == "fmt ",
          tagAt(b, 0) + " " + tagAt(b, 12));
    check(name + " take: samples at byte " + to_string(dataStart) + ", sizes match the frames",
          frames > 0 && tagAt(b, dataTag) == "data" && u32At(b, dataTag + 4) == dataBytes
          && (uint64_t)b.size() == dataStart + dataBytes && u32At(b, 4) == b.size() - 8
          && (!isF32 || u32At(b, 80) == frames),
          to_string(frames) + " frames, " + to_string(b.size()) + " bytes");

    SoundBuffer buf;
    const LoadResult r = buf.load(wav);
    float peak = 0.0f;
    for (float s : buf.samples) peak = max(peak, fabs(s));
    check(name + " take loads through SoundBuffer",
          r.ok() && buf.channels == 2 && buf.sampleRate == sampleRate, r.message);
    check(name + " take: numSamples == getRecordedSeconds() * sampleRate",
          buf.numSamples == frames, to_string(buf.numSamples) + " vs " + to_string(frames));
    check(name + " take holds the tone (the loader reads the samples, not the header)",
          peak > 0.1f && peak <= 1.0f, to_string(peak));

    std::error_code ec;
    fs::remove(wav, ec);
}

// A take whose writes fail: /dev/full opens fine and fails every flush with
// ENOSPC, like a full disk or a file that hit the FAT32 size limit.
static void recordIntoFullDevice() {
#if defined(__linux__)
    const size_t noticesBefore = countNotices("[AudioRecorder]");
    AudioRecorder rec;
    const bool started = rec.start(fs::path("/dev/full"));
    // Past the stream's buffer, so writes fail before stop().
    const bool some = started && waitFor([&] { return rec.getRecordedSeconds() >= 0.25; }, 5000);
    rec.stop();
    check("a take into /dev/full starts and counts frames", started && some);
    check("... stop() logs the failed write as an error",
          countErrors("[AudioRecorder] writing /dev/full failed") == 1);
    // start() logs "recording ->"; stop() must add neither the RF64 notice
    // nor "stopped:", which would report the counted frames as written.
    check("... and no RF64 or \"stopped\" notice",
          countNotices("[AudioRecorder]") == noticesBefore + 1
          && countNotices("RF64") == 0,
          to_string(countNotices("[AudioRecorder]") - noticesBefore) + " notices");
#else
    printf("%-72s SKIP (no /dev/full)\n", "a take into /dev/full logs an error on stop()");
#endif
}

int main() {
    // A recorder that never finishes would hang CI; fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    checkSizeFields();
    checkHeaderBytes();

    // Device-less engine; set before anything opens a context.
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();   // this thread is the main thread

    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        lock_guard<mutex> lock(g_logMutex);
        if (e.level == LogLevel::Notice) g_notices.push_back(e.message);
        else if (e.level == LogLevel::Error) g_errors.push_back(e.message);
    });

    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = 48000;
    settings.channels = 2;
    settings.bufferSize = 256;
    const bool started = engine.init(settings);
    check("engine starts on the null backend", started && engine.isInitialized());
    if (!started) return 1;

    Sound tone;
    tone.loadTestTone(440.0f, 10.0f);   // amplitude 0.5
    check("a test tone plays into the mix", tone.play());

    recordTake(AudioRecordSettings::SampleFormat::S16, settings.sampleRate);
    recordTake(AudioRecordSettings::SampleFormat::F32, settings.sampleRate);
    check("a short take logs no RF64 notice", countNotices("RF64") == 0);
    check("... and both takes log their stop", countNotices("[AudioRecorder] stopped") == 2);
    check("... and no error", countErrors("[AudioRecorder]") == 0);

    recordIntoFullDevice();

    tone.stop();
    engine.shutdown();
    logSub.disconnect();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
