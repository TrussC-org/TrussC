// =============================================================================
// core/tests/audioDiagnostics — behavioral regression test for #231: a play
// the AudioEngine refuses must not vanish silently.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The engine runs on miniaudio's null backend
// (internal::setNullAudioBackendForTests()), a device-less clock that still
// drives the real mixer callback, so no sound card is needed.
//
// Guards the invariants:
// - Sound::play() returns false when the play is dropped, for every reason
//   (voice limit, a stream's maxPolyphony, a stream file that cannot be
//   reopened, no running device), and each drop is counted in getStats().
// - Drops reach the TrussC logger (onLog / log file / logcat), not stdout:
//   immediately on the main thread, rate limited, and never from a thread
//   other than the main one (off-main drops are only counted; the main
//   thread's pumpAudioDiagnostics() reports them).
// - The audio thread meters the output: peak / RMS / clipped samples, the
//   per-voice level and the audio-thread load.
// - The app loop does the reporting itself: runHeadlessApp's frame pump logs
//   an off-main drop with no help from the test, and its exit flush logs
//   what the rate limit still held back; AudioEngine::shutdown() flushes too,
//   and clears the meters (no device, no output).
// - tc_get_audio_state reports the same numbers over MCP, including the
//   microphone (MicInput on the null backend: its device name while it runs).
// - A reused SoundBuffer's getPath() follows its last fill: a file load sets
//   it, a memory decode, PCM fill or generator clears it (a voice must not
//   report the previous file).
// - A file named .ogg that is not Ogg Vorbis fails to load (DecodeFailed,
//   logged), and the file is closed once: stb_vorbis closes it on the failed
//   open, SoundBuffer does not close it again (counted by fcloseProbe.cpp on
//   Linux).
// - SoundBuffer::loadPcmFromMemory() validates the format before it touches
//   the buffer: a channel count below 1, or a byte size that is not a whole
//   number of frames, fails (and is logged); a valid load leaves numSamples *
//   channels == samples.size(); 32-bit big-endian samples are byte-swapped.
//   A failed load keeps channels, sampleRate, numSamples and the path.
//   The sample count the loaders size buffers with is checked against the
//   limit before the product is formed: a product that would wrap 64 bits is
//   refused, and a 32-bit limit stands in for a 32-bit size_t. (The 64-bit
//   frame size in loadPcmFromMemory only matters on a 32-bit build.)
//   The loaders' 32-bit arithmetic is covered by the helper checks with
//   maxCount = 0xFFFFFFFF, not by a 32-bit build of this test.
// - SoundBuffer::mixFrom() counts its offset in frames (samples per channel,
//   like numSamples) and mixes with the channel stride; buffers of different
//   channel counts, and an offset whose end wraps or passes what a buffer
//   holds, are refused and logged with the buffer untouched.
// - Decoders size buffers from what decodes, not from the stated stream
//   length: the first reservation is capped by the input's size
//   (internal::decodeReserveSamples), and a FLAC whose STREAMINFO states
//   more samples than it holds loads the frames it has without any
//   allocation of the stated size (allocProbe.cpp records the largest
//   operator new request of the load). Past the reservation the buffer
//   grows geometrically but lands on a correctly stated length, so a FLAC
//   that decodes to more than its reservation still ends with one buffer
//   of exactly its length. The same holds for Ogg Vorbis (vorbisTone.cpp),
//   whose stated length (the last page's granule) may also be unknown.
//   Near a memory limit the buffer grows in smaller steps, whether the limit
//   shows as a refused check (setAllocationLimitForTests, as on the web) or
//   as a std::bad_alloc (allocProbe.cpp, elsewhere).
// - A voice on a buffer with no frames, or with fewer samples than
//   numSamples * channels, stops at its first mix (looping or not), and
//   setPosition() on an empty buffer lands on 0.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// --- Log capture -------------------------------------------------------------
struct LogEntry {
    LogLevel level;
    string message;
    thread::id tid;
};
static mutex g_logMutex;
static vector<LogEntry> g_logs;

static size_t countLogs(LogLevel level, const string& needle) {
    lock_guard<mutex> lock(g_logMutex);
    size_t n = 0;
    for (auto& e : g_logs) {
        if (e.level == level && e.message.find(needle) != string::npos) ++n;
    }
    return n;
}

static bool allLogsOnMainThread() {
    lock_guard<mutex> lock(g_logMutex);
    for (auto& e : g_logs) {
        if (e.tid != getMainThreadId()) return false;
    }
    return true;
}

static string lastLog(LogLevel level) {
    lock_guard<mutex> lock(g_logMutex);
    for (auto it = g_logs.rbegin(); it != g_logs.rend(); ++it) {
        if (it->level == level) return it->message;
    }
    return "";
}

// --- Helpers -----------------------------------------------------------------

#if defined(__linux__) && !defined(__ANDROID__)
// fcloseProbe.cpp: counts this thread's fclose() calls between arm and disarm
void fcloseProbeArm();
void fcloseProbeDisarm();
int fcloseProbeCloses();
int fcloseProbeRepeats();
#endif

// allocProbe.cpp: the largest operator new request between arm and disarm
void allocProbeArm();
void allocProbeDisarm();
size_t allocProbeLargest();
// allocProbe.cpp: requests on this thread above `bytes` throw std::bad_alloc (0: off)
void allocProbeFailAbove(size_t bytes);

// vorbisTone.cpp: 10000 frames of 8 kHz stereo Ogg Vorbis
vector<char> vorbisToneBytes();

// 16-bit mono PCM WAV with a 440 Hz tone.
static bool writeWav(const fs::path& path, float seconds, int rate) {
    const uint32_t frames = (uint32_t)(seconds * rate);
    const uint32_t dataBytes = frames * 2;
    ofstream f(path, ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32((uint32_t)rate); u32((uint32_t)rate * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        u16((uint16_t)(int16_t)(8000.0f * sin(TAU * 440.0f * (float)i / (float)rate)));
    }
    return (bool)f;
}

// Wait (up to `timeoutMs`) until `pred()` holds.
template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 10) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    return pred();
}

// Longer than the engine's report interval (2 s).
static void waitReportInterval() { this_thread::sleep_for(chrono::milliseconds(2200)); }

// Call tc_get_audio_state through the MCP server, return its payload.
static Json callAudioState(const string& arguments) {
    string reply = mcp::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"tc_get_audio_state","arguments":)"
        + arguments + "}}");
    try {
        Json j = Json::parse(reply);
        return Json::parse(j.at("result").at("content").at(0).at("text").get<string>());
    } catch (...) {
        return Json();
    }
}

// runHeadlessApp phase: an off-main drop in setup() must be logged by the
// loop's own frame pump (the app only watches the log); on the way out one
// more off-main drop, inside the rate-limit window, must be logged by the
// loop's exit flush.
static Sound* g_copy = nullptr;         // a streamed Sound's copy past maxPolyphony
static string g_offMainLine;
static size_t g_linesBefore = 0;
static atomic<bool> g_pumpSeen{false};

static void playCopyOffMain() {
    thread worker([] { g_copy->play(); });
    worker.join();
}

// --- SoundBuffer::loadPcmFromMemory: format and size validation ---------------
// A load either fills the buffer so that numSamples * channels ==
// samples.size() (what the mixer indexes by), or fails and leaves the buffer
// as it was.
static bool pcmConsistent(const SoundBuffer& b) {
    return b.channels > 0 && b.numSamples * (size_t)b.channels == b.samples.size();
}

static void checkPcmLoading() {
    const size_t kSizeMax = numeric_limits<size_t>::max();

    // Sample count products: checked before they are formed. maxCount =
    // UINT32_MAX stands in for a 32-bit size_t's limit.
    {
        size_t n = 0;
        // Zero frames: only the channel check refuses these (the limit check
        // alone would accept them).
        check("pcm: sample count of 0 channels is refused",
              !internal::interleavedSampleCount(0, 0, kSizeMax, n));
        check("pcm: sample count of negative channels is refused",
              !internal::interleavedSampleCount(0, -2, kSizeMax, n));
        // Formed first, this product wraps a 64-bit size_t to 4.
        check("pcm: sample count wrapping 64 bits is refused",
              !internal::interleavedSampleCount(0x4000000000000001ull, 4, kSizeMax, n));
        check("pcm: sample count at a 32-bit size_t's limit is accepted",
              internal::interleavedSampleCount(0x3FFFFFFFull, 4, 0xFFFFFFFFu, n) && n == 0xFFFFFFFCu,
              to_string(n));
        check("pcm: sample count past a 32-bit limit is refused",
              !internal::interleavedSampleCount(0x40000001ull, 4, 0xFFFFFFFFu, n));
        check("pcm: longest OGG length x 16 channels is refused on a 32-bit size_t",
              !internal::interleavedSampleCount(0xFFFFFFFFull, 16, 0xFFFFFFFFu, n));
        check("pcm: sample count exactly at maxCount is accepted",
              internal::interleavedSampleCount(333, 3, 999, n) && n == 999);
        check("pcm: sample count one frame past maxCount is refused",
              !internal::interleavedSampleCount(334, 3, 1001, n));
        // On this platform's own size_t and vector limit (refused on 32-bit)
        const vector<float> v;
        const bool fits = internal::interleavedSampleCount(0xFFFFFFFFull, 16, v.max_size(), n);
        check("pcm: longest OGG length x 16 channels fits only with a 64-bit size_t",
              fits == (sizeof(size_t) >= 8), to_string(sizeof(size_t) * 8) + "-bit size_t");
    }

    SoundBuffer buf;
    const int16_t stereo16[6] = {32767, -32768, 0, 1, -1, 16384};
    check("pcm: 16-bit stereo loads",
          (bool)buf.loadPcmFromMemory(stereo16, sizeof(stereo16), 2, 48000) &&
          buf.numSamples == 3 && pcmConsistent(buf),
          to_string(buf.numSamples) + " frames");
    check("pcm: 16-bit extremes convert to [-1, 1)",
          buf.samples.size() == 6 && buf.samples[0] == 32767 / 32768.0f && buf.samples[1] == -1.0f &&
          buf.samples[2] == 0.0f && buf.samples[3] == 1 / 32768.0f && buf.samples[5] == 0.5f);

    // A failed load keeps the buffer's format; every failing call below passes
    // a different rate, so an early sampleRate assignment shows.
    auto unchanged = [&] {
        return buf.channels == 2 && buf.sampleRate == 48000 && buf.numSamples == 3 &&
               pcmConsistent(buf);
    };

    // Invalid channel counts fail and leave the buffer as it was
    const size_t errorsBefore = countLogs(LogLevel::Error, "invalid PCM channel count");
    for (int ch : {0, -1, numeric_limits<int>::min()}) {
        const LoadResult r = buf.loadPcmFromMemory(stereo16, sizeof(stereo16), ch, 22050);
        check("pcm: " + to_string(ch) + " channels fails with UnsupportedFormat",
              !r && r.error == LoadError::UnsupportedFormat, loadErrorName(r.error));
    }
    check("pcm: each invalid channel count is logged",
          countLogs(LogLevel::Error, "invalid PCM channel count") == errorsBefore + 3,
          lastLog(LogLevel::Error));
    check("pcm: a failed load leaves the buffer as it was", unchanged(),
          to_string(buf.sampleRate) + " Hz");

    // A byte size that is not a whole number of frames
    auto sizeFails = [&](const string& name, const void* data, size_t size, int ch, int bits) {
        const size_t logsBefore = countLogs(LogLevel::Error, "PCM data ");
        const LoadResult r = buf.loadPcmFromMemory(data, size, ch, 22050, bits);
        check("pcm: " + name + " fails with DecodeFailed",
              !r && r.error == LoadError::DecodeFailed, loadErrorName(r.error));
        check("pcm: " + name + " is logged once",
              countLogs(LogLevel::Error, "PCM data ") == logsBefore + 1, lastLog(LogLevel::Error));
        check("pcm: " + name + " leaves the buffer as it was", unchanged(),
              to_string(buf.sampleRate) + " Hz");
    };
    const float floats[8] = {0.25f, -0.25f, 1.0f, -1.0f, 0.5f, -0.5f, 0.125f, -0.125f};
    sizeFails("16-bit mono, odd byte count", stereo16, 3, 1, 16);
    sizeFails("16-bit stereo, half a frame over", stereo16, 6, 2, 16);
    sizeFails("32-bit mono, 4k+2 bytes", floats, 10, 1, 32);
    sizeFails("32-bit mono, 4k+3 bytes", floats, 11, 1, 32);
    sizeFails("32-bit stereo, 3 samples", floats, 12, 2, 32);
    // The frame size itself is past 32 bits (4 * 2^30 bytes wraps to 0 in a
    // 32-bit size_t); nothing is read.
    sizeFails("32-bit frame of 2^30 channels", floats, 8, 1 << 30, 32);
    sizeFails("16-bit frame of INT_MAX channels", floats, 8, numeric_limits<int>::max(), 16);
    // Whole frames, but more samples than a vector can hold; nothing is read.
    sizeFails("16-bit mono, more samples than a buffer holds", stereo16, kSizeMax - 1, 1, 16);

    // 32-bit float: copied exactly, sized by the sample count
    check("pcm: 32-bit stereo loads",
          (bool)buf.loadPcmFromMemory(floats, sizeof(floats), 2, 44100, 32) && buf.numSamples == 4 &&
          pcmConsistent(buf) && memcmp(buf.samples.data(), floats, sizeof(floats)) == 0);
    check("pcm: 32-bit mono loads with one sample",
          (bool)buf.loadPcmFromMemory(floats, 4, 1, 44100, 32) && buf.numSamples == 1 &&
          pcmConsistent(buf) && buf.samples[0] == 0.25f);
    // Big-endian float: bytes of 1.0f, -0.5f, 0.25f in network order
    const uint8_t beFloats[12] = {0x3F, 0x80, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00,
                                  0x3E, 0x80, 0x00, 0x00};
    check("pcm: 32-bit big-endian samples are byte-swapped",
          (bool)buf.loadPcmFromMemory(beFloats, sizeof(beFloats), 1, 44100, 32, true) &&
          buf.numSamples == 3 && pcmConsistent(buf) && buf.samples[0] == 1.0f &&
          buf.samples[1] == -0.5f && buf.samples[2] == 0.25f,
          buf.samples.empty() ? "" : to_string(buf.samples[0]));

    // Many channels, one frame each
    vector<float> wide(4096, 0.75f);
    check("pcm: one 4096-channel float frame loads",
          (bool)buf.loadPcmFromMemory(wide.data(), wide.size() * sizeof(float), 4096, 48000, 32) &&
          buf.numSamples == 1 && buf.channels == 4096 && pcmConsistent(buf) && buf.samples[4095] == 0.75f);
    vector<int16_t> wide16(4096 * 2, -16384);
    check("pcm: two 4096-channel 16-bit frames load",
          (bool)buf.loadPcmFromMemory(wide16.data(), wide16.size() * sizeof(int16_t), 4096, 48000) &&
          buf.numSamples == 2 && pcmConsistent(buf) && buf.samples.back() == -0.5f);

    // No data is zero frames, as before
    check("pcm: zero bytes load as an empty buffer",
          (bool)buf.loadPcmFromMemory(stereo16, 0, 2, 48000) && buf.numSamples == 0 &&
          buf.samples.empty() && buf.channels == 2);
}

// --- SoundBuffer::mixFrom: frames, channel stride, checked end ----------------
// offsetSamples counts samples per channel (the unit of numSamples); the mix
// runs frame by frame with the channel stride and keeps numSamples * channels
// == samples.size().
static void checkMixFrom() {
    auto stereo = [](const vector<float>& interleaved) {
        SoundBuffer b;
        b.loadPcmFromMemory(interleaved.data(), interleaved.size() * sizeof(float), 2, 48000, 32);
        return b;
    };
    const SoundBuffer other = stereo({0.5f, -0.5f, 0.25f, -0.25f});   // 2 frames (L, R)

    SoundBuffer base = stereo({1, 2, 3, 4, 5, 6, 7, 8});                  // 4 frames
    base.mixFrom(other, 3);
    check("mix: a stereo mix past the end grows by frames",
          base.numSamples == 5 && pcmConsistent(base), to_string(base.numSamples) + " frames");
    check("mix: the offset counts frames, channels keep their stride",
          base.samples.size() == 10 && base.samples[5] == 6 && base.samples[6] == 7.5f &&
          base.samples[7] == 7.5f && base.samples[8] == 0.25f && base.samples[9] == -0.25f);

    base = stereo({1, 2, 3, 4, 5, 6, 7, 8});
    base.mixFrom(other, 1, 2.0f);
    check("mix: a stereo mix inside the buffer adds frame by frame",
          base.numSamples == 4 && pcmConsistent(base) && base.samples[1] == 2 &&
          base.samples[2] == 4 && base.samples[3] == 3 && base.samples[4] == 5.5f &&
          base.samples[5] == 5.5f && base.samples[6] == 7);

    // `other` claiming more frames than it holds mixes only the frames it has
    SoundBuffer shortOther = other;
    shortOther.numSamples = 1000;
    base = stereo({1, 2, 3, 4});
    base.mixFrom(shortOther, 0);
    check("mix: only the whole frames `other` holds are mixed",
          base.numSamples == 2 && pcmConsistent(base) && base.samples[3] == 3.75f,
          to_string(base.numSamples) + " frames");

    // Refused: nothing is mixed or resized, and the refusal is logged
    const vector<float> before = {1, 2, 3, 4, 5, 6, 7, 8};
    auto refused = [&](const string& name, const SoundBuffer& src, size_t offset,
                       const string& logNeedle) {
        SoundBuffer b = stereo(before);
        const size_t logsBefore = countLogs(LogLevel::Error, logNeedle);
        b.mixFrom(src, offset);
        check("mix: " + name + " is refused", b.samples == before && b.numSamples == 4);
        check("mix: " + name + " is logged",
              countLogs(LogLevel::Error, logNeedle) == logsBefore + 1, lastLog(LogLevel::Error));
    };
    SoundBuffer mono;
    mono.generateSineWave(440.0f, 0.01f, 0.5f, 48000);
    refused("a mono buffer into a stereo one", mono, 0, "channel counts differ");
    // An offset whose end would wrap a size_t is refused
    const size_t kSizeMax = numeric_limits<size_t>::max();
    refused("an offset of SIZE_MAX", other, kSizeMax, "past what a buffer holds");
    refused("an offset of SIZE_MAX - 1", other, kSizeMax - 1, "past what a buffer holds");
    // The end fits a size_t, but not end * channels in a vector
    refused("an end past max_size() / channels", other, before.max_size() / 2,
            "past what a buffer holds");
}

// --- Decoder buffer sizing ----------------------------------------------------
// 16-bit mono PCM WAV of `frames` frames (440 Hz tone)
static vector<char> wavBytes(uint32_t frames, int rate) {
    vector<char> out;
    auto put = [&](const void* p, size_t n) {
        out.insert(out.end(), (const char*)p, (const char*)p + n);
    };
    auto u32 = [&](uint32_t v) { put(&v, 4); };
    auto u16 = [&](uint16_t v) { put(&v, 2); };
    put("RIFF", 4); u32(36 + frames * 2); put("WAVE", 4);
    put("fmt ", 4); u32(16); u16(1); u16(1); u32((uint32_t)rate); u32((uint32_t)rate * 2); u16(2); u16(16);
    put("data", 4); u32(frames * 2);
    for (uint32_t i = 0; i < frames; ++i) u16((uint16_t)(int16_t)(8000.0f * sin(TAU * 440.0f * (float)i / (float)rate)));
    return out;
}

// 16-bit mono 48 kHz FLAC of `blocks` frames of 4096 samples, each a
// CONSTANT subframe of 1000, whose STREAMINFO states `statedSamples` samples
// (36-bit field).
static vector<char> flacBytes(int blocks, uint64_t statedSamples) {
    vector<uint8_t> out = {'f', 'L', 'a', 'C', 0x80, 0x00, 0x00, 34};   // last block: STREAMINFO
    // STREAMINFO: min / max block size 4096, min / max frame size unknown,
    // then 64 bits of sample rate (20), channels - 1 (3), bits - 1 (5) and
    // total samples (36)
    vector<uint8_t> si = {0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0};
    const uint64_t packed = ((uint64_t)48000 << 44) | ((uint64_t)15 << 36) |
                            (statedSamples & 0xFFFFFFFFFull);
    for (int i = 0; i < 8; ++i) si.push_back((uint8_t)(packed >> (56 - 8 * i)));
    out.insert(out.end(), si.begin(), si.end());
    out.insert(out.end(), 16, 0);   // MD5 unknown
    auto crc8 = [](const uint8_t* p, size_t n) {
        uint8_t c = 0;
        for (size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int b = 0; b < 8; ++b) c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
        }
        return c;
    };
    auto crc16 = [](const uint8_t* p, size_t n) {
        uint16_t c = 0;
        for (size_t i = 0; i < n; ++i) {
            c ^= (uint16_t)(p[i] << 8);
            for (int b = 0; b < 8; ++b) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1);
        }
        return c;
    };
    for (int f = 0; f < blocks; ++f) {
        // sync + fixed blocking, block size 4096 / 48 kHz, mono / 16-bit, frame number
        vector<uint8_t> fr = {0xFF, 0xF8, 0xCA, 0x08, (uint8_t)f};
        fr.push_back(crc8(fr.data(), fr.size()));
        fr.insert(fr.end(), {0x00, 0x03, 0xE8});   // CONSTANT subframe: 1000
        const uint16_t c = crc16(fr.data(), fr.size());
        fr.push_back((uint8_t)(c >> 8));
        fr.push_back((uint8_t)c);
        out.insert(out.end(), fr.begin(), fr.end());
    }
    return vector<char>(out.begin(), out.end());
}

static void checkDecodeSizing(const fs::path& dir, const string& tag) {
    // The reservation: the stated length, capped by the format's samples per
    // input byte and by the limit. UINT32_MAX stands in for a 32-bit size_t.
    {
        using internal::decodeReserveSamples;
        const size_t kSizeMax = numeric_limits<size_t>::max();
        const uint64_t k16 = internal::kReserveSamplesPerInputByte;
        check("decode: a stated length below the input's cap is reserved as stated",
              decodeReserveSamples(1000, 2, 1000, k16, kSizeMax) == 2000);
        check("decode: a stated length above the input's cap is capped by the input",
              decodeReserveSamples(0xFFFFFFFFull, 2, 1000, k16, kSizeMax) == 16000);
        check("decode: the MP3 cap is 48 samples per input byte",
              decodeReserveSamples(0xFFFFFFFFull, 2, 1000,
                                   internal::kReserveSamplesPerInputByteMp3, kSizeMax) == 48000);
        check("decode: the Vorbis cap is 32 samples per input byte",
              decodeReserveSamples(0xFFFFFFFFull, 2, 1000,
                                   internal::kReserveSamplesPerInputByteVorbis, kSizeMax) == 32000);
        // A 60-minute 44.1 kHz stereo MP3 at 32 kbit/s (14.4 MB): the stated
        // length is below the MP3 cap, so it is reserved in full
        check("decode: a low-bitrate MP3's stated length is reserved in full",
              decodeReserveSamples(3600ull * 44100, 2, 3600ull * 4000,
                                   internal::kReserveSamplesPerInputByteMp3, kSizeMax) ==
                  3600ull * 44100 * 2);
        check("decode: an unknown input size reserves nothing",
              decodeReserveSamples(0xFFFFFFFFull, 2, 0, k16, kSizeMax) == 0);
        check("decode: stated frames x channels saturates instead of wrapping",
              decodeReserveSamples(0x8000000000000001ull, 4, 1000, k16, kSizeMax) == 16000);
        check("decode: input bytes x the rate saturates instead of wrapping",
              decodeReserveSamples(1000, 2, 0x1000000000000001ull, k16, kSizeMax) == 2000);
        check("decode: the reservation stops at a 32-bit limit",
              decodeReserveSamples(0xFFFFFFFFull, 16, 0xFFFFFFFFull, k16, 0xFFFFFFFFu) == 0xFFFFFFFFu);
        check("decode: a longest 36-bit length stops at a 32-bit limit",
              decodeReserveSamples(0xFFFFFFFFFull, 8, 0xFFFFFFFFFull, k16, 0xFFFFFFFFu) == 0xFFFFFFFFu);
        check("decode: no channels reserve nothing",
              decodeReserveSamples(1000, 0, 1000, k16, kSizeMax) == 0);
    }

    // Growth past the reservation: double the capacity, land on the stated
    // length when it lies in between, never below what is needed
    {
        using internal::growSampleBuffer;
        vector<float> v;
        v.reserve(1000);
        check("grow: doubles the capacity", growSampleBuffer(v, 1001) && v.capacity() == 2000,
              to_string(v.capacity()));
        v = vector<float>();
        v.reserve(1000);
        check("grow: lands on a stated length below double",
              growSampleBuffer(v, 1001, 1500) && v.capacity() == 1500, to_string(v.capacity()));
        v = vector<float>();
        v.reserve(1000);
        check("grow: never follows a stated length past double",
              growSampleBuffer(v, 1001, 1u << 30) && v.capacity() == 2000, to_string(v.capacity()));
        v = vector<float>();
        v.reserve(1000);
        check("grow: ignores a stated length below what is needed",
              growSampleBuffer(v, 1001, 500) && v.capacity() == 2000, to_string(v.capacity()));
        v = vector<float>();
        v.reserve(1000);
        check("grow: takes at least what is needed",
              growSampleBuffer(v, 5000, 3000) && v.capacity() == 5000, to_string(v.capacity()));
        check("grow: refuses past max_size()", !growSampleBuffer(v, v.max_size() + 1) &&
                                                   v.capacity() == 5000);
    }

    // Growth under a memory limit (internal::setAllocationLimitForTests
    // refuses what the web's malloc probe would): when the doubled size does
    // not fit, the largest of the smaller steps that fits is taken, so the
    // buffer is reallocated a logarithmic number of times on its way to the
    // limit rather than once per append, and growth fails as soon as the
    // next append no longer fits.
    {
        using internal::growSampleBuffer;
        using internal::setAllocationLimitForTests;
        vector<float> v;
        v.reserve(1000);
        setAllocationLimitForTests(1300 * sizeof(float));
        const bool stepped = growSampleBuffer(v, 1001);
        setAllocationLimitForTests(0);
        check("grow: under a limit takes the largest smaller step that fits",
              stepped && v.capacity() == 1250, to_string(v.capacity()));
        v = vector<float>();
        v.reserve(1000);
        setAllocationLimitForTests(1001 * sizeof(float));
        const bool exact = growSampleBuffer(v, 1001);
        setAllocationLimitForTests(0);
        check("grow: under a limit takes what is needed when only that fits",
              exact && v.capacity() == 1001, to_string(v.capacity()));

        // 1024 samples per append from 600 towards a limit of 2^20: doubling
        // stops at 614400, and appending up to the limit takes 424 appends
        const size_t limit = (size_t)1 << 20;
        const size_t step = 1024;
        v = vector<float>();
        v.reserve(600);
        setAllocationLimitForTests(limit * sizeof(float));
        int reallocs = 0;
        bool refused = false;
        for (size_t i = 0; i <= limit / step; ++i) {
            const size_t before = v.capacity();
            if (!growSampleBuffer(v, v.size() + step)) {
                refused = true;
                break;
            }
            if (v.capacity() != before) ++reallocs;
            v.resize(v.size() + step);
        }
        const size_t fullSize = v.size();
        const size_t fullCapacity = v.capacity();
        const bool refusedAgain = !growSampleBuffer(v, v.size() + step);
        setAllocationLimitForTests(0);
        check("grow: under a limit it fills up to the limit, then refuses",
              refused && fullSize == limit && fullCapacity == limit,
              to_string(fullSize) + " samples, capacity " + to_string(fullCapacity));
        check("grow: under a limit it reallocates a logarithmic number of times",
              reallocs <= 30, to_string(reallocs) + " reallocations");
        check("grow: past the limit it keeps refusing with the buffer unchanged",
              refusedAgain && v.capacity() == fullCapacity && v.size() == fullSize);
    }

#ifndef __EMSCRIPTEN__
    // Growth where a reserve throws std::bad_alloc (allocProbeFailAbove), with
    // no allocationFits limit set: the same smaller steps are tried, and
    // growth is refused only when `needed` cannot be allocated either.
    // (Web builds do not catch exceptions; there allocationFits decides.)
    {
        using internal::growSampleBuffer;
        auto growFailingAbove = [](vector<float>& v, size_t needed, size_t failAboveBytes,
                                   bool& threw) {
            bool grown = false;
            threw = false;
            allocProbeFailAbove(failAboveBytes);
            try {
                grown = growSampleBuffer(v, needed);
            } catch (const bad_alloc&) {
                threw = true;
            }
            allocProbeFailAbove(0);
            return grown;
        };
        bool threw = false;
        vector<float> v;
        v.reserve(1000);
        const bool stepped = growFailingAbove(v, 1001, 1300 * sizeof(float), threw);
        check("grow: a failing reserve falls back to the largest smaller step",
              stepped && !threw && v.capacity() == 1250,
              to_string(v.capacity()) + (threw ? ", threw" : ""));
        v = vector<float>();
        v.reserve(1000);
        const bool exact = growFailingAbove(v, 1001, 1001 * sizeof(float), threw);
        check("grow: a failing reserve falls back to what is needed",
              exact && !threw && v.capacity() == 1001,
              to_string(v.capacity()) + (threw ? ", threw" : ""));
        v = vector<float>();
        v.reserve(1000);
        const bool refused = !growFailingAbove(v, 1001, 1000 * sizeof(float), threw);
        check("grow: when no reserve succeeds it refuses with the buffer unchanged",
              refused && !threw && v.capacity() == 1000,
              to_string(v.capacity()) + (threw ? ", threw" : ""));
    }
#endif

    // A FLAC whose STREAMINFO states far more samples than its frames hold
    // (a 36-bit field): the load gets the frames that decode, and no
    // allocation along the way is sized from the stated length (2^27 frames
    // = 512 MiB as floats). The bound: the reservation cap (16 samples per
    // input byte) plus 1 MiB for the decode step and the decoder's own state.
    const vector<char> flac = flacBytes(2, (uint64_t)1 << 27);
    const size_t capBytes = flac.size() * 16 * sizeof(float) + (1u << 20);
    auto flacIntact = [](const SoundBuffer& buf, int blocks) {
        if (buf.numSamples != (size_t)blocks * 4096 || buf.channels != 1 || !pcmConsistent(buf)) {
            return false;
        }
        for (float v : buf.samples) if (v != 1000 / 32768.0f) return false;
        return true;
    };
    SoundBuffer m;
    allocProbeArm();
    const bool memOk = (bool)m.loadFlacFromMemory(flac.data(), flac.size());
    allocProbeDisarm();
    check("decode: a FLAC stating more samples than it holds loads from memory",
          memOk && flacIntact(m, 2), to_string(m.numSamples) + " frames");
    check("decode: no allocation of the load is sized from the stated length",
          allocProbeLargest() <= capBytes, to_string(allocProbeLargest()) + " bytes requested");
    check("decode: the buffer keeps no capacity from the stated length",
          m.samples.capacity() <= m.samples.size() + m.samples.size() / 8,
          to_string(m.samples.capacity()));
    const fs::path flacPath = dir / ("tc_audio_diag_" + tag + "_stated.flac");
    {
        ofstream out(flacPath, ios::binary);
        out.write(flac.data(), (streamsize)flac.size());
    }
    SoundBuffer f;
    allocProbeArm();
    const bool fileOk = (bool)f.loadFlac(flacPath);
    allocProbeDisarm();
    check("decode: the same FLAC loads from a file", fileOk && flacIntact(f, 2),
          to_string(f.numSamples) + " frames");
    check("decode: no allocation of the file load is sized from the stated length",
          allocProbeLargest() <= capBytes, to_string(allocProbeLargest()) + " bytes requested");
    std::error_code ec;
    fs::remove(flacPath, ec);

    // A FLAC of silence-like CONSTANT frames decodes to far more than its
    // reservation (16 samples per byte) while stating its length correctly:
    // it grows geometrically, lands on the stated length, and at no point
    // asks for more than that one buffer (about 2 MiB of floats here; 127
    // blocks keep the frame number in one byte).
    const int longBlocks = 127;
    const uint64_t longFrames = (uint64_t)longBlocks * 4096;
    const vector<char> longFlac = flacBytes(longBlocks, longFrames);
    SoundBuffer lf;
    allocProbeArm();
    const bool longOk = (bool)lf.loadFlacFromMemory(longFlac.data(), longFlac.size());
    allocProbeDisarm();
    check("decode: a FLAC outgrowing its reservation loads every frame",
          longOk && flacIntact(lf, longBlocks), to_string(lf.numSamples) + " frames");
    check("decode: its growth lands on the stated length",
          lf.samples.capacity() == lf.samples.size(), to_string(lf.samples.capacity()));
    // Up to 4 KiB over the stated length: MSVC's allocator adds a few
    // bytes to every aligned request of 4 KiB or more
    check("decode: no allocation of it goes past the stated length",
          allocProbeLargest() <= longFrames * sizeof(float) + 4096,
          to_string(allocProbeLargest()) + " bytes requested");

    // The same FLAC under a memory limit below its length: the load fails
    // with an error once the buffer cannot grow any further
    SoundBuffer limited;
    internal::setAllocationLimitForTests((size_t)(longFrames / 2) * sizeof(float));
    const bool limitedOk = (bool)limited.loadFlacFromMemory(longFlac.data(), longFlac.size());
    internal::setAllocationLimitForTests(0);
    check("decode: a FLAC that does not fit under a memory limit fails to load", !limitedOk,
          to_string(limited.numSamples) + " frames");

#ifndef __EMSCRIPTEN__
    // A FLAC stating more than it holds, where every reserve above its
    // decoded length (plus 4 KiB for MSVC's allocator) throws
    // std::bad_alloc: the doubled size fails, smaller steps are taken, and
    // the load still gets every frame
    {
        const vector<char> overstated = flacBytes(longBlocks, (uint64_t)1 << 27);
        SoundBuffer tight;
        allocProbeFailAbove((size_t)longFrames * sizeof(float) + 4096);
        bool tightOk = false;
        bool threw = false;
        try {
            tightOk = (bool)tight.loadFlacFromMemory(overstated.data(), overstated.size());
        } catch (const bad_alloc&) {
            threw = true;
        }
        allocProbeFailAbove(0);
        check("decode: a FLAC loads when a reserve near its length fails",
              tightOk && !threw && flacIntact(tight, longBlocks),
              to_string(tight.numSamples) + " frames" + (threw ? ", threw" : ""));
    }
#endif

    // A WAV decodes in steps to exactly its frames
    const uint32_t frames = 10000;   // two full steps and a partial one
    const vector<char> wav = wavBytes(frames, 48000);
    SoundBuffer w;
    bool same = (bool)w.loadWavFromMemory(wav.data(), wav.size()) && w.numSamples == frames &&
                pcmConsistent(w);
    for (uint32_t i = 0; same && i < frames; ++i) {
        same = w.samples[i] == (float)(int16_t)(8000.0f * sin(TAU * 440.0f * (float)i / 48000.0f)) / 32768.0f;
    }
    check("decode: a WAV loads every frame intact", same, to_string(w.numSamples) + " frames");
}

// --- Ogg Vorbis sizing ----------------------------------------------------------
// `ogg` with the granule position of its last page set to `granule` (the
// length stb_vorbis reports; all ones reads as unknown, 0) and the page's
// CRC recomputed
static vector<char> withLastGranule(vector<char> ogg, uint64_t granule) {
    size_t page = string::npos;
    for (size_t i = 0; i + 27 <= ogg.size(); ++i) {
        if (memcmp(&ogg[i], "OggS", 4) == 0) page = i;
    }
    if (page == string::npos) return {};
    const uint8_t segments = (uint8_t)ogg[page + 26];
    size_t length = 27 + segments;
    for (size_t k = 0; k < segments; ++k) length += (uint8_t)ogg[page + 27 + k];
    if (page + length > ogg.size()) return {};
    for (int k = 0; k < 8; ++k) ogg[page + 6 + k] = (char)(granule >> (8 * k));
    for (int k = 0; k < 4; ++k) ogg[page + 22 + k] = 0;
    uint32_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint32_t)(uint8_t)ogg[page + i] << 24;
        for (int b = 0; b < 8; ++b) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    for (int k = 0; k < 4; ++k) ogg[page + 22 + k] = (char)(crc >> (8 * k));
    return ogg;
}

// Ogg Vorbis goes through drainVorbis: samples are appended as they decode,
// numSamples is the decoded frame count, the stated length (the last page's
// granule) only sizes the reservation (at most 32 samples per input byte),
// and a stream whose length is unknown loads what decodes.
static void checkVorbisSizing(const fs::path& dir, const string& tag) {
    const vector<char> ogg = vorbisToneBytes();
    const size_t capBytes = ogg.size() * 32 * sizeof(float) + (1u << 20);
    check("vorbis: the embedded stream is intact", ogg.size() == 4036 &&
                                                   memcmp(ogg.data(), "OggS", 4) == 0,
          to_string(ogg.size()) + " bytes");

    SoundBuffer ref;
    allocProbeArm();
    const bool refOk = (bool)ref.loadOggFromMemory(ogg.data(), ogg.size());
    allocProbeDisarm();
    check("vorbis: a stream loads from memory with its frame count",
          refOk && ref.numSamples == 10000 && ref.channels == 2 && ref.sampleRate == 8000 &&
          pcmConsistent(ref),
          to_string(ref.numSamples) + " frames, " + to_string(ref.channels) + " ch");
    check("vorbis: a correctly stated length is reserved exactly",
          ref.samples.capacity() == ref.samples.size(), to_string(ref.samples.capacity()));
    // Left 440 Hz at 0.5, right 660 Hz at 0.25: the channels keep their order
    float peakL = 0, peakR = 0;
    for (size_t i = 0; refOk && i < ref.numSamples; ++i) {
        peakL = max(peakL, fabs(ref.samples[i * 2]));
        peakR = max(peakR, fabs(ref.samples[i * 2 + 1]));
    }
    check("vorbis: the channels decode in order",
          peakL > 0.4f && peakL < 0.6f && peakR > 0.15f && peakR < 0.35f,
          to_string(peakL) + " / " + to_string(peakR));
    check("vorbis: the load stays within the reservation bound",
          allocProbeLargest() <= capBytes, to_string(allocProbeLargest()) + " bytes requested");

    const fs::path oggPath = dir / ("tc_audio_diag_" + tag + "_tone.ogg");
    {
        ofstream out(oggPath, ios::binary);
        out.write(ogg.data(), (streamsize)ogg.size());
    }
    SoundBuffer file;
    check("vorbis: the same stream loads from a file",
          (bool)file.loadOgg(oggPath) && file.numSamples == ref.numSamples &&
              file.samples == ref.samples,
          to_string(file.numSamples) + " frames");
    std::error_code ec;
    fs::remove(oggPath, ec);

    // Stating a little more than decodes (10300 frames; the last packet then
    // decodes untrimmed to 10240): the small spare capacity is kept rather
    // than copying the whole buffer to drop it.
    {
        const vector<char> patched = withLastGranule(ogg, 10300);
        SoundBuffer b;
        const bool ok = !patched.empty() && (bool)b.loadOggFromMemory(patched.data(), patched.size());
        check("vorbis: a slightly overstated stream loads what decodes",
              ok && b.numSamples == 10240 && pcmConsistent(b), to_string(b.numSamples) + " frames");
        check("vorbis: its small spare capacity is not trimmed by a copy",
              b.samples.capacity() == 10300 * 2, to_string(b.samples.capacity()));
    }

    // The same stream stating 2^31 frames (16 GiB as stereo floats), and
    // stating no length at all: both load what decodes (the last packet
    // untrimmed, so a few frames more), without an allocation sized from
    // the stated length.
    const struct { const char* name; uint64_t granule; } variants[] = {
        {"a stream stating 2^31 frames", (uint64_t)1 << 31},
        {"a stream of unknown length", ~(uint64_t)0},
    };
    for (const auto& variant : variants) {
        const vector<char> patched = withLastGranule(ogg, variant.granule);
        SoundBuffer b;
        allocProbeArm();
        const bool ok = !patched.empty() && (bool)b.loadOggFromMemory(patched.data(), patched.size());
        allocProbeDisarm();
        const string name = string("vorbis: ") + variant.name;
        check(name + " loads what decodes",
              ok && b.numSamples >= ref.numSamples && b.numSamples < ref.numSamples + 4096 &&
                  pcmConsistent(b) &&
                  equal(ref.samples.begin(), ref.samples.end(), b.samples.begin()),
              to_string(b.numSamples) + " frames");
        check(name + " stays within the reservation bound", allocProbeLargest() <= capBytes,
              to_string(allocProbeLargest()) + " bytes requested");
        check(name + " keeps no capacity from the stated length",
              b.samples.capacity() <= b.samples.size() + b.samples.size() / 8,
              to_string(b.samples.capacity()));
    }
}

// --- Voices on buffers with nothing to play ------------------------------------
// A voice on an empty buffer (or one whose samples are shorter than
// numSamples * channels) stops at its first mix; setPosition() on an empty
// buffer lands on 0.
static void checkEmptyVoices() {
    auto stops = [](Sound& s) {
        return waitFor([&] { return !s.isPlaying(); }, 1000);
    };

    auto empty = make_shared<SoundBuffer>();
    const int16_t none[1] = {};
    check("voice: an empty buffer loads", (bool)empty->loadPcmFromMemory(none, 0, 2, 48000) &&
                                             empty->numSamples == 0);
    Sound looping;
    looping.loadFromBuffer(empty);
    looping.setLoop(true);
    check("voice: a looping voice on an empty buffer starts", looping.play());
    check("voice: a looping voice on an empty buffer stops", stops(looping));
    looping.stop();

    Sound seek;
    seek.loadFromBuffer(empty);
    check("voice: a voice on an empty buffer starts again", seek.play());
    seek.setPosition(1.0f);
    check("voice: setPosition() on an empty buffer lands on 0", seek.getPosition() == 0.0f,
          to_string(seek.getPosition()));
    check("voice: after setPosition() it stops", stops(seek));
    seek.stop();

    // numSamples claims more frames than samples holds
    auto shortBuf = make_shared<SoundBuffer>();
    shortBuf->channels = 2;
    shortBuf->sampleRate = 48000;
    shortBuf->numSamples = 48000;
    shortBuf->samples.assign(10, 0.25f);
    Sound shortVoice;
    shortVoice.loadFromBuffer(shortBuf);
    shortVoice.setLoop(true);
    check("voice: a looping voice on a short buffer starts", shortVoice.play());
    check("voice: a looping voice on a short buffer stops", stops(shortVoice));
    shortVoice.stop();
}

struct PumpApp : App {
    int frames = 0;
    void setup() override { playCopyOffMain(); }
    void update() override {
        ++frames;
        if (!g_pumpSeen && countLogs(LogLevel::Warning, g_offMainLine) == g_linesBefore + 1) {
            g_pumpSeen = true;
            playCopyOffMain();   // held back by the rate limit: only the exit flush logs it
            requestExit();
        }
        if (frames >= 400) requestExit();   // ~2 s: the pump never reported
    }
};

int main() {
    // Device-less engine; set before anything opens a context.
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();   // this thread is the main thread

    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        lock_guard<mutex> lock(g_logMutex);
        g_logs.push_back({e.level, e.message, this_thread::get_id()});
    });

    // --- engine start ---------------------------------------------------------
    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = 48000;
    settings.channels = 2;
    settings.bufferSize = 256;
    settings.maxPolyphony = 2;
    const bool started = engine.init(settings);
    check("engine starts on the null backend", started && engine.isInitialized());
    if (!started) return 1;
    auto report = internal::audioDeviceReport(false);
    check("device report names the Null backend", report.backend == "Null", report.backend);
    check("device report has the granted period", report.periodFrames > 0);
    // Plumbing: the null backend's one playback device is the enumerated default.
    check("the default-device flag reaches the device report", report.outputIsDefault);
    check("init is logged through the logger", countLogs(LogLevel::Notice, "[AudioEngine] initialized") == 1);

    // --- voice limit ------------------------------------------------------------
    Sound a, b, c;
    a.loadTestTone(440.0f, 10.0f);
    b.loadTestTone(550.0f, 10.0f);
    c.loadTestTone(660.0f, 10.0f);
    check("play() returns true while a voice is free", a.play() && b.play());
    check("play() returns false when every voice is busy", !c.play());
    AudioStats st = engine.getStats();
    check("voice-limit drop is counted", st.droppedVoiceLimit == 1 && st.droppedPlays == 1,
          to_string(st.droppedVoiceLimit) + "/" + to_string(st.droppedPlays));
    check("drop is logged as a warning right away",
          countLogs(LogLevel::Warning, "voices are busy") == 1, lastLog(LogLevel::Warning));

    // --- rate limit: repeats are summed into one later line ---------------------
    for (int i = 0; i < 5; ++i) c.play();
    internal::pumpAudioDiagnostics();
    st = engine.getStats();
    check("repeated drops are all counted", st.droppedVoiceLimit == 6, to_string(st.droppedVoiceLimit));
    check("repeated drops inside the interval are not logged one by one",
          countLogs(LogLevel::Warning, "dropped") == 1);
    waitReportInterval();
    internal::pumpAudioDiagnostics();
    check("the pump reports them in one summary line",
          countLogs(LogLevel::Warning, "5 plays dropped since the last report") == 1,
          lastLog(LogLevel::Warning));

    // --- off the main thread: count only, the main thread reports ---------------
    // Past the interval, so a main-thread drop would be logged at once: the
    // worker's drop must still only be counted.
    waitReportInterval();
    const size_t dropLinesBefore = countLogs(LogLevel::Warning, "dropped");
    atomic<bool> offThreadResult{true};
    thread worker([&] { offThreadResult = c.play(); });
    worker.join();
    check("play() off the main thread returns false when dropped", !offThreadResult.load());
    check("off-main drop is counted", engine.getStats().droppedVoiceLimit == 7);
    check("nothing is logged from the other thread",
          allLogsOnMainThread() && countLogs(LogLevel::Warning, "dropped") == dropLinesBefore);
    internal::pumpAudioDiagnostics();
    check("the main thread reports the off-main drop",
          countLogs(LogLevel::Warning, "1 play dropped since the last report") == 1,
          lastLog(LogLevel::Warning));
    check("every log line came from the main thread", allLogsOnMainThread());

    // --- output meters and voice snapshot ---------------------------------------
    a.stop();
    b.stop();
    SoundBuffer loudBuf;
    loudBuf.generateSineWave(440.0f, 10.0f, 0.5f, 48000);
    Sound loud;
    loud.loadFromBuffer(loudBuf);
    loud.setVolume(4.0f);   // 0.5 * 4 = peaks at 2.0: must clip
    check("loud voice plays", loud.play());
    const bool metered = waitFor([&] {
        AudioStats s = engine.getStats();
        return s.clippedSamples > 0 && s.peak > 1.5f;
    }, 2000);
    st = engine.getStats();
    check("clipped samples are counted", st.clippedSamples > 0);
    check("master peak is measured before the clamp", metered && st.peak > 1.5f && st.peak < 2.1f,
          to_string(st.peak));
    check("master RMS is measured", st.rms > 0.5f, to_string(st.rms));
    check("audio-thread load is published", waitFor([&] { return engine.getStats().load > 0.0f; }, 1500));

    auto voices = engine.getVoices();
    check("getVoices() lists the one playing voice", voices.size() == 1, to_string(voices.size()));
    if (voices.size() == 1) {
        const auto& v = voices[0];
        check("voice: eager, no file, volume 4", !v.streaming && v.path.empty() && v.volume == 4.0f);
        check("voice: level is the pre-clip peak of its output", v.level > 1.5f && v.level < 2.1f,
              to_string(v.level));
        check("voice: position advances", v.position > 0.0f && v.duration > 9.0f);
    }
    loud.pause();
    voices = engine.getVoices();
    check("paused voice reports level 0", voices.size() == 1 && voices[0].paused && voices[0].level == 0.0f);
    loud.stop();

    // --- streams: maxPolyphony drop, file path ----------------------------------
    const string tag = to_string((long long)chrono::steady_clock::now().time_since_epoch().count());
    const fs::path wav  = fs::temp_directory_path() / ("tc_audio_diag_" + tag + ".wav");
    const fs::path wav2 = fs::temp_directory_path() / ("tc_audio_diag_" + tag + "_gone.wav");
    check("test WAV files written", writeWav(wav, 2.0f, 48000) && writeWav(wav2, 1.0f, 48000));

    Sound s1;
    check("loadStream() succeeds", (bool)s1.loadStream(wav, 1));
    Sound s2 = s1;   // copies share one SoundStream (and its maxPolyphony)
    check("stream plays", s1.play());
    check("a copy past the stream's maxPolyphony is refused", !s2.play());
    st = engine.getStats();
    check("stream-limit drop is counted", st.droppedStreamLimit == 1, to_string(st.droppedStreamLimit));
    check("stream-limit drop names the file",
          countLogs(LogLevel::Warning, "maxPolyphony=1 reached for " + internal::pathToUtf8(wav)) == 1,
          lastLog(LogLevel::Warning));
    voices = engine.getVoices();
    check("stream voice reports its file",
          voices.size() == 1 && voices[0].streaming && voices[0].path == internal::pathToUtf8(wav));
    s1.stop();

    Sound eager;
    check("eager load() from a file", (bool)eager.load(wav));
    check("the load is logged through the logger (verbose)",
          countLogs(LogLevel::Verbose, "loaded WAV " + internal::pathToUtf8(wav)) == 1);
    check("eager voice plays", eager.play());
    voices = engine.getVoices();
    check("eager voice reports its file",
          voices.size() == 1 && !voices[0].streaming && voices[0].path == internal::pathToUtf8(wav));

    // A reused SoundBuffer: getPath() (a voice's "file") follows the last fill.
    {
        SoundBuffer reused;
        check("path: loadWav() records the file", (bool)reused.loadWav(wav) && reused.getPath() == wav,
              internal::pathToUtf8(reused.getPath()));
        ifstream in(wav, ios::binary);
        const vector<char> bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        check("path: the same WAV from memory clears it",
              (bool)reused.loadWavFromMemory(bytes.data(), bytes.size()) && reused.getPath().empty(),
              internal::pathToUtf8(reused.getPath()));
        check("path: load() of the file records it again", (bool)reused.load(wav) && reused.getPath() == wav);
        const int16_t pcm[64] = {};
        check("path: a failed PCM load keeps it",
              !reused.loadPcmFromMemory(pcm, 3, 1, 48000) && reused.getPath() == wav &&
              !reused.loadPcmFromMemory(pcm, sizeof(pcm), 0, 48000) && reused.getPath() == wav,
              internal::pathToUtf8(reused.getPath()));
        check("path: PCM from memory clears it",
              (bool)reused.loadPcmFromMemory(pcm, sizeof(pcm), 1, 48000) && reused.getPath().empty());
        reused.loadWav(wav);
        reused.generateSineWave(440.0f, 0.1f);
        check("path: a generated wave clears it", reused.getPath().empty());
    }

    checkPcmLoading();
    checkMixFrom();
    checkDecodeSizing(fs::temp_directory_path(), tag);
    checkVorbisSizing(fs::temp_directory_path(), tag);
    checkEmptyVoices();

    // A file named .ogg that is not Ogg Vorbis: the load fails and is
    // reported, and the file is closed once (stb_vorbis closes it on the
    // failed open), through loadOgg() and through load()'s dispatch.
    {
        const fs::path notOgg = fs::temp_directory_path() / ("tc_audio_diag_" + tag + "_text.ogg");
        {
            ofstream out(notOgg, ios::binary);
            out << "plain text, not an Ogg Vorbis stream\n";
        }
        check("ogg: text file with an .ogg extension written", fs::exists(notOgg));
        const string openFailedLine = internal::pathToUtf8(notOgg) + " (stb_vorbis error=";
        for (int via = 0; via < 2; ++via) {
            const string name = via == 0 ? "ogg: loadOgg()" : "ogg: load()";
            SoundBuffer buf;
            const size_t errorsBefore = countLogs(LogLevel::Error, openFailedLine);
#if defined(__linux__) && !defined(__ANDROID__)
            fcloseProbeArm();
#endif
            const LoadResult r = via == 0 ? buf.loadOgg(notOgg) : buf.load(notOgg);
#if defined(__linux__) && !defined(__ANDROID__)
            fcloseProbeDisarm();
#endif
            check(name + " of a non-Vorbis file fails with DecodeFailed",
                  !r && r.error == LoadError::DecodeFailed, loadErrorName(r.error));
            check(name + " failure is logged",
                  countLogs(LogLevel::Error, openFailedLine) == errorsBefore + 1, lastLog(LogLevel::Error));
#if defined(__linux__) && !defined(__ANDROID__)
            check(name + " closes the file once", fcloseProbeCloses() == 1,
                  to_string(fcloseProbeCloses()) + " fclose calls");
            check(name + " does not close the file again", fcloseProbeRepeats() == 0,
                  to_string(fcloseProbeRepeats()) + " repeat closes");
#endif
        }
        std::error_code rmEc;
        fs::remove(notOgg, rmEc);
    }

    Sound gone;
    check("loadStream() of the second file", (bool)gone.loadStream(wav2, 1));
    std::error_code ec;
    fs::remove(wav2, ec);
    check("play() of a stream whose file vanished returns false", !gone.play());
    st = engine.getStats();
    check("decoder-error drop is counted", st.droppedDecoderError == 1, to_string(st.droppedDecoderError));
    check("decoder-error drop is logged", countLogs(LogLevel::Warning, "could not reopen") == 1);

    // --- MCP: tc_get_audio_state -----------------------------------------------
    mcp::registerInspectionTools();
    Json state = callAudioState("{}");
    check("tc_get_audio_state answers", state.is_object() && state.value("status", "") == "ok", state.dump());
    if (state.is_object() && state.contains("output")) {
        st = engine.getStats();
        check("tool: engine running on the Null backend",
              state.value("running", false) && state["output"].value("backend", "") == "Null");
        check("tool: dropped counts match getStats()",
              state["dropped"].value("total", (uint64_t)0) == st.droppedPlays &&
              state["dropped"].value("voiceLimit", (uint64_t)0) == 7 &&
              state["dropped"].value("streamLimit", (uint64_t)0) == 1 &&
              state["dropped"].value("decoderError", (uint64_t)0) == 1,
              state["dropped"].dump());
        check("tool: voices list the playing file",
              state["voices"].size() == 1 &&
              state["voices"][0].value("file", "") == internal::pathToUtf8(wav.lexically_normal()),
              state["voices"].dump());
        check("tool: master meters and thread load present",
              state["master"].contains("peak") && state["master"].value("clippedSamples", (uint64_t)0) > 0 &&
              state["thread"].value("load", 0.0f) > 0.0f && state["thread"].contains("loadMax"),
              state.dump());
        check("tool: input section present", state["input"].contains("running"));
        check("tool: devices enumerated by default",
              state.contains("devices") && state["devices"]["playback"].size() >= 1);
    }
    Json lean = callAudioState(R"({"devices":false})");
    check("tool: devices=false skips the enumeration", lean.is_object() && !lean.contains("devices"));

    // The microphone opens the null backend's capture device too.
    {
        MicInput& mic = getMicInput();
        check("mic: start() on the null backend", mic.start() && mic.isRunning());
        const string micName = mic.getDeviceName();
        check("mic: getDeviceName() names the opened device", !micName.empty());
        Json withMic = callAudioState(R"({"devices":false})");
        check("tool: input reports the running mic and its device",
              withMic["input"].value("running", false) && withMic["input"].value("device", "") == micName &&
              !micName.empty(), withMic["input"].dump());
        mic.stop();
        check("mic: stop() clears the device name", !mic.isRunning() && mic.getDeviceName().empty());
        Json noMic = callAudioState(R"({"devices":false})");
        check("tool: input reports the stopped mic",
              !noMic["input"].value("running", true) && noMic["input"].value("device", "x").empty(),
              noMic["input"].dump());
    }
    eager.stop();

    // --- the app loop pumps by itself, and flushes at exit ----------------------
    {
        Sound hs1;
        check("loadStream() for the app-loop case", (bool)hs1.loadStream(wav, 1));
        Sound hs2 = hs1;     // a copy: past the stream's maxPolyphony while hs1 plays
        hs1.setLoop(true);   // still playing after the waits below
        check("app-loop stream plays", hs1.play());
        g_copy = &hs2;
        waitReportInterval();   // a stream-limit line is due again
        g_offMainLine = "1 play dropped since the last report: a SoundStream";
        g_linesBefore = countLogs(LogLevel::Warning, g_offMainLine);
        HeadlessSettings hs;
        hs.setFps(200.0f);
        runHeadlessApp<PumpApp>(hs);
        check("app loop: the frame pump logged the off-main drop", g_pumpSeen.load());
        check("app loop: the exit flush logged the held-back drop",
              countLogs(LogLevel::Warning, g_offMainLine) == g_linesBefore + 2,
              lastLog(LogLevel::Warning));
        check("app loop: every log line came from the main thread", allLogsOnMainThread());
        g_copy = nullptr;
        hs1.stop();
    }

    // --- shutdown: flush what is held back, clear the meters --------------------
    Sound busy1, busy2, extra;
    busy1.loadTestTone(440.0f, 10.0f);
    busy2.loadTestTone(550.0f, 10.0f);
    extra.loadTestTone(660.0f, 10.0f);
    busy1.setVolume(3.0f);
    check("two voices fill the pool", busy1.play() && busy2.play());
    check("the loud voice is metered", waitFor([&] { return engine.getStats().peak > 1.0f; }, 2000));
    const string heldLine = "1 play dropped since the last report: every voice slot busy";
    const size_t heldBefore = countLogs(LogLevel::Warning, heldLine);
    check("a first drop (logged now) and a second (held back)", !extra.play() && !extra.play());
    engine.shutdown();
    check("shutdown flushes the held-back drop", countLogs(LogLevel::Warning, heldLine) == heldBefore + 1,
          lastLog(LogLevel::Warning));
    check("shutdown is logged through the logger", countLogs(LogLevel::Notice, "[AudioEngine] shutdown") == 1);
    st = engine.getStats();
    check("shutdown clears the meters",
          st.peak == 0.0f && st.rms == 0.0f && st.load == 0.0f && st.loadMax == 0.0f,
          to_string(st.peak));
    voices = engine.getVoices();
    check("voices left in their slots report level 0 after shutdown",
          !voices.empty() && voices[0].level == 0.0f, to_string(voices.size()));
    busy1.stop();
    busy2.stop();

    // --- no running device ------------------------------------------------------
    check("play() returns false with no running device", !eager.play());
    st = engine.getStats();
    check("not-running drop is counted", st.droppedNotRunning == 1);
    check("not-running drop is logged", countLogs(LogLevel::Warning, "no output device is running") == 1);
    check("unloaded Sound: play() returns false", !Sound().play());

    fs::remove(wav, ec);
    logSub.disconnect();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
