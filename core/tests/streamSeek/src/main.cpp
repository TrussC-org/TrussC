// =============================================================================
// core/tests/streamSeek — behavioral regression test for #280: seeking a
// streamed Sound moves the audio, and a stream that cannot be read ends
// instead of spinning the StreamWorker thread.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The engine runs on miniaudio's null backend
// (internal::setNullAudioBackendForTests()), a device-less clock that still
// drives the real mixer callback, so no sound card is needed. The test
// listens on AudioEngine::audioOut and reads the level of the mix: every
// test file holds DC levels, so the level tells which part of the file is
// playing.
//
// Guards the invariants:
// - setPosition() on a stream moves the audio (the decoder seeks and the
//   ring refills), not only what getPosition() reports.
// - getPosition() on a stream reports the requested target from the call
//   on, never the old position, until the audio has moved there; then the
//   position being played, which starts at the target.
// - A paused stream reports the target at once, the voice itself does not
//   move while paused (the mixer is the only writer of its position), and it
//   resumes from the target.
// - Repeated seeks: the last one wins, while playing and while paused.
// - Eager sounds seek at once.
// - loadStream() rejects a file with no audio frames (DecodeFailed).
// - A looping stream whose decoder returns no frames (the file was emptied
//   after loadStream()) ends with one warning, and the worker goes on
//   refilling the other streams.
// - A decoder read error, a failed loop seek and a failed seek request each
//   end the stream with one warning (a test hook makes the decoder fail);
//   after a failed seek request a non-looping voice ends.
// A watchdog turns a StreamWorker that never comes back into a FAIL.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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
static mutex g_logMutex;
static vector<pair<LogLevel, string>> g_logs;

static size_t countLogs(LogLevel level, const string& needle) {
    lock_guard<mutex> lock(g_logMutex);
    size_t n = 0;
    for (auto& e : g_logs) {
        if (e.first == level && e.second.find(needle) != string::npos) ++n;
    }
    return n;
}

static string lastLog(LogLevel level) {
    lock_guard<mutex> lock(g_logMutex);
    for (auto it = g_logs.rbegin(); it != g_logs.rend(); ++it) {
        if (it->first == level) return it->second;
    }
    return "";
}

// --- Helpers -----------------------------------------------------------------
constexpr int kRate = 48000;   // engine rate = file rate: no resampling

// 16-bit stereo PCM WAV made of DC segments {seconds, level}. No segments
// writes an empty data chunk.
static bool writeWav(const fs::path& path, const vector<pair<float, float>>& segments) {
    uint32_t frames = 0;
    for (auto& s : segments) frames += (uint32_t)lround(s.first * kRate);
    const uint32_t dataBytes = frames * 4;
    ofstream f(path, ios::binary | ios::trunc);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32((uint32_t)kRate); u32((uint32_t)kRate * 4);
    u16(4); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (auto& s : segments) {
        const uint32_t n = (uint32_t)lround(s.first * kRate);
        const int16_t v = (int16_t)lround(s.second * 32768.0f);
        for (uint32_t i = 0; i < n; ++i) { u16((uint16_t)v); u16((uint16_t)v); }
    }
    return (bool)f;
}

// Mean of the left channel over the last audioOut block.
static atomic<float> g_level{0.0f};

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

static bool near(float a, float b, float tol) { return fabs(a - b) <= tol; }

// Wait (up to `timeoutMs`) until `pred()` holds.
template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 5) {
        if (pred()) return true;
        sleepMs(5);
    }
    return pred();
}

// Position of the (single) streamed voice as the engine plays it.
static float streamVoicePosition() {
    for (auto& v : AudioEngine::getInstance().getVoices()) {
        if (v.streaming) return v.position;
    }
    return -1.0f;
}

// setPosition(target), then watch getPosition() and the level for 200 ms:
// getPosition() never drops below the target, it is still close to the
// target when the level reaches `newLevel`, and the level 200 ms after the
// call is sampled.
struct SeekTrace {
    float first = -1.0f;      // getPosition() right after the call
    float minSeen = 1e9f;     // lowest getPosition() in the 200 ms
    float atSwitch = -1.0f;   // getPosition() when the level switched
    bool switched = false;
    float levelAt200 = -1.0f; // level 200 ms after the call
};

static SeekTrace seekAndTrace(Sound& s, float target, float newLevel) {
    SeekTrace t;
    const auto t0 = chrono::steady_clock::now();
    s.setPosition(target);
    t.first = s.getPosition();
    while (chrono::steady_clock::now() - t0 < chrono::milliseconds(195)) {
        const float p = s.getPosition();
        if (p < t.minSeen) t.minSeen = p;
        if (!t.switched && near(g_level.load(), newLevel, 0.03f)) {
            t.atSwitch = s.getPosition();
            t.switched = true;
        }
        sleepMs(1);
    }
    this_thread::sleep_until(t0 + chrono::milliseconds(200));
    t.levelAt200 = g_level.load();
    return t;
}

int main() {
    // A StreamWorker stuck in one stream never returns, and its static
    // destructor would then hang the exit: fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout (the StreamWorker did not come back)\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    // Device-less engine; set before anything opens a context.
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();   // this thread is the main thread

    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        lock_guard<mutex> lock(g_logMutex);
        g_logs.push_back({e.level, e.message});
    });

    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = kRate;
    settings.channels = 2;
    settings.bufferSize = 256;
    settings.maxPolyphony = 8;
    if (!engine.init(settings)) {
        printf("SKIP: the audio engine does not start on the null backend here\n");
        return 0;
    }

    EventListener levelSub = engine.audioOut.listen([](AudioOutBuffer& b) {
        double sum = 0.0;
        for (int i = 0; i < b.frameCount; ++i) sum += b.data[i * b.channels];
        g_level.store(b.frameCount > 0 ? (float)(sum / b.frameCount) : 0.0f);
    });

    const fs::path dir = fs::temp_directory_path() / ("tc_streamSeek_" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path dcWav = dir / "dc.wav";        // 0.1 for 0-1 s, 0.5 for 1-3 s
    const fs::path bgmWav = dir / "bgm.wav";      // 0.5 for 3 s
    const fs::path shortWav = dir / "short.wav";  // 0.1 for 0.2 s
    const fs::path emptyWav = dir / "empty.wav";  // no frames
    const fs::path vanishWav = dir / "vanish.wav";
    check("test files are written",
          writeWav(dcWav, {{1.0f, 0.1f}, {2.0f, 0.5f}}) && writeWav(bgmWav, {{3.0f, 0.5f}}) &&
          writeWav(shortWav, {{0.2f, 0.1f}}) && writeWav(emptyWav, {}) &&
          writeWav(vanishWav, {{0.5f, 0.1f}}));

    // --- seek while playing ------------------------------------------------------
    Sound s;
    check("loadStream() opens the DC file", (bool)s.loadStream(dcWav) && s.isStreaming());
    check("the stream plays", s.play());
    sleepMs(150);
    check("it plays from the start (level 0.1)", near(g_level.load(), 0.1f, 0.02f),
          to_string(g_level.load()));

    SeekTrace t = seekAndTrace(s, 2.0f, 0.5f);
    check("getPosition() is the target right after setPosition()", near(t.first, 2.0f, 0.001f),
          to_string(t.first));
    check("the audio moved to the target (level 0.5, 200 ms after the seek)",
          near(t.levelAt200, 0.5f, 0.02f), to_string(t.levelAt200));
    check("getPosition() never reported the old position meanwhile", t.minSeen >= 2.0f - 0.001f,
          to_string(t.minSeen));
    check("getPosition() is still the target when the audio moves",
          t.switched && t.atSwitch >= 2.0f - 0.001f && t.atSwitch < 2.1f, to_string(t.atSwitch));
    float p = s.getPosition();
    check("then getPosition() follows the playback from the target", p > 2.05f && p < 2.3f,
          to_string(p));

    // --- seek while paused -------------------------------------------------------
    s.pause();
    sleepMs(50);
    const float voiceBefore = streamVoicePosition();
    s.setPosition(0.25f);
    check("paused: getPosition() is the target at once", near(s.getPosition(), 0.25f, 0.001f),
          to_string(s.getPosition()));
    sleepMs(100);
    check("paused: getPosition() stays at the target", near(s.getPosition(), 0.25f, 0.001f),
          to_string(s.getPosition()));
    const float voiceAfter = streamVoicePosition();
    check("paused: the voice itself has not moved (the mixer applies the seek)",
          voiceBefore > 2.0f && near(voiceAfter, voiceBefore, 0.001f),
          to_string(voiceBefore) + " -> " + to_string(voiceAfter));
    s.resume();
    sleepMs(200);
    check("resumed: it plays from the target (level 0.1)", near(g_level.load(), 0.1f, 0.02f),
          to_string(g_level.load()));
    p = s.getPosition();
    check("resumed: getPosition() follows from the target", p > 0.3f && p < 0.65f, to_string(p));

    // --- repeated seeks: the last one wins ---------------------------------------
    s.setPosition(2.5f);
    s.setPosition(0.2f);
    s.setPosition(1.8f);
    check("repeated: getPosition() is the last target", near(s.getPosition(), 1.8f, 0.001f),
          to_string(s.getPosition()));
    sleepMs(200);
    p = s.getPosition();
    check("repeated: the audio is at the last target (level 0.5)",
          near(g_level.load(), 0.5f, 0.02f), to_string(g_level.load()));
    check("repeated: getPosition() follows from the last target", p > 1.85f && p < 2.2f,
          to_string(p));

    // Paused: the worker serves the first request while the second waits for
    // the mixer, which only runs again on resume.
    s.pause();
    s.setPosition(2.6f);
    sleepMs(50);
    s.setPosition(0.4f);
    check("repeated while paused: getPosition() is the last target",
          near(s.getPosition(), 0.4f, 0.001f), to_string(s.getPosition()));
    s.resume();
    sleepMs(200);
    p = s.getPosition();
    check("repeated while paused: the audio is at the last target (level 0.1)",
          near(g_level.load(), 0.1f, 0.02f), to_string(g_level.load()));
    check("repeated while paused: getPosition() follows from the last target",
          p > 0.45f && p < 0.8f, to_string(p));
    s.stop();

    // --- eager sounds seek at once -----------------------------------------------
    Sound eager;
    check("the DC file loads eagerly", (bool)eager.load(dcWav) && !eager.isStreaming());
    eager.play();
    sleepMs(100);
    eager.setPosition(2.0f);
    check("eager: getPosition() is the target at once", near(eager.getPosition(), 2.0f, 0.01f),
          to_string(eager.getPosition()));
    sleepMs(50);
    check("eager: the audio moved (level 0.5)", near(g_level.load(), 0.5f, 0.02f),
          to_string(g_level.load()));
    eager.stop();

    // --- zero-length file --------------------------------------------------------
    {
        Sound z;
        const LoadResult r = z.loadStream(emptyWav);
        check("loadStream() of a file with no frames fails with DecodeFailed",
              !r && r.error == LoadError::DecodeFailed && !z.isLoaded(), loadErrorName(r.error));
        check("the empty file is logged", countLogs(LogLevel::Error, "no audio frames") == 1,
              lastLog(LogLevel::Error));
    }

    // --- a looping stream emptied after loading: ends, the others play on ---------
    {
        Sound bgm, vanish;
        check("a second stream plays", (bool)bgm.loadStream(bgmWav) && bgm.play());
        check("the stream to empty loads", (bool)vanish.loadStream(vanishWav));
        // The file goes empty (a failed recording overwritten, storage gone
        // quiet): the voice's own decoder opens it with no frames.
        check("the file is emptied after loadStream()", writeWav(vanishWav, {}));
        vanish.setLoop(true);
        const size_t before = countLogs(LogLevel::Warning, "no frames to read");
        check("the emptied stream starts", vanish.play());
        check("the emptied looping stream ends with a warning",
              waitFor([&] { return countLogs(LogLevel::Warning, "no frames to read") == before + 1; },
                      1000),
              lastLog(LogLevel::Warning));
        sleepMs(1000);   // longer than the other stream's ring holds
        check("the other stream is still refilled (level 0.5 a second later)",
              near(g_level.load(), 0.5f, 0.02f), to_string(g_level.load()));
        check("the warning is logged once",
              countLogs(LogLevel::Warning, "no frames to read") == before + 1);
        vanish.stop();
        bgm.stop();
    }

    // --- decoder read error ------------------------------------------------------
    {
        Sound c;
        check("a looping stream plays", (bool)c.loadStream(dcWav) && (c.setLoop(true), c.play()));
        sleepMs(100);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::ReadFails);
        check("a read error ends the stream with a warning",
              waitFor([] { return countLogs(LogLevel::Warning, "decoder read failed") == 1; }, 1000),
              lastLog(LogLevel::Warning));
        sleepMs(600);   // the ring drains; nothing new is decoded
        check("the read error is logged once",
              countLogs(LogLevel::Warning, "decoder read failed") == 1);
        check("after a read error the voice falls silent", near(g_level.load(), 0.0f, 0.001f),
              to_string(g_level.load()));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        c.stop();

        Sound after;
        check("the worker refills a new stream afterwards", (bool)after.loadStream(bgmWav) &&
              after.play() && waitFor([] { return near(g_level.load(), 0.5f, 0.02f); }, 500),
              to_string(g_level.load()));
        after.stop();
    }

    // --- failed seek at the loop point -------------------------------------------
    {
        Sound e;
        check("a short looping stream plays", (bool)e.loadStream(shortWav) && (e.setLoop(true), e.play()));
        sleepMs(100);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::SeekFails);
        check("a failed loop seek ends the stream with a warning",
              waitFor([] { return countLogs(LogLevel::Warning, "seek to the start for the loop failed") == 1; },
                      1000),
              lastLog(LogLevel::Warning));
        sleepMs(300);
        check("the failed loop seek is logged once",
              countLogs(LogLevel::Warning, "seek to the start for the loop failed") == 1);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        e.stop();
    }

    // --- failed seek request -----------------------------------------------------
    {
        Sound f;
        check("a stream plays", (bool)f.loadStream(dcWav) && f.play());
        sleepMs(100);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::SeekFails);
        f.setPosition(2.0f);
        check("a failed seek request ends the stream with a warning",
              waitFor([] { return countLogs(LogLevel::Warning, "seek to frame") == 1; }, 1000),
              lastLog(LogLevel::Warning));
        check("after a failed seek the voice ends", waitFor([&] { return !f.isPlaying(); }, 1000));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        f.stop();
    }

    levelSub.disconnect();   // the listener only touches g_level: no barrier needed
    engine.shutdown();
    fs::remove_all(dir, ec);
    logSub.disconnect();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
