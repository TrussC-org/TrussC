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
// - AudioEngine::init() that can't open the output device returns false,
//   leaves the engine uninitialized and logs one error through the logger,
//   naming the requested device (also one that was not found and fell back
//   to the default); a later init() succeeds (#279).
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

    // --- init() failure: logged, device named, callable again (#279) ------------
    // miniaudio's ma_device_init refuses more than MA_MAX_CHANNELS (254)
    // channels on the null backend too, which stands in for an output device
    // that can't be opened. (ma_device_start failing has no such trigger.)
    {
        const string failLine = "[AudioEngine] failed to initialize ";
        AudioSettings bad = settings;
        bad.channels = 255;
        size_t before = countLogs(LogLevel::Error, failLine);
        check("init() returns false when the output device can't be opened",
              !engine.init(bad) && !engine.isInitialized());
        check("... logged once through logError(\"AudioEngine\")",
              countLogs(LogLevel::Error, failLine) == before + 1
              && lastLog(LogLevel::Error).find("failed to initialize the output device (result=") != string::npos,
              lastLog(LogLevel::Error));

        // A requested device that exists is named.
        const auto devices = AudioEngine::listDevices();
        const string devName = devices.empty() ? string() : devices[0].name;
        bad.deviceName = devName;
        before = countLogs(LogLevel::Error, failLine);
        const bool namedFailed = !devName.empty() && !engine.init(bad);
        check("a failed init names the requested device",
              namedFailed && countLogs(LogLevel::Error, failLine) == before + 1
              && lastLog(LogLevel::Error).find("the output device '" + devName + "'") != string::npos,
              lastLog(LogLevel::Error));

        // A requested device that is missing: the default it fell back to failed.
        bad.deviceName = "tc-test-no-such-device";
        before = countLogs(LogLevel::Error, failLine);
        check("a failed init names a requested device that wasn't found",
              !engine.init(bad) && countLogs(LogLevel::Error, failLine) == before + 1
              && lastLog(LogLevel::Error).find("requested 'tc-test-no-such-device' was not found")
                 != string::npos,
              lastLog(LogLevel::Error));

        check("init() called again later succeeds", engine.init(settings) && engine.isInitialized());
        engine.shutdown();
    }

    fs::remove(wav, ec);
    logSub.disconnect();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
