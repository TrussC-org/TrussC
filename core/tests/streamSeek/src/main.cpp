// =============================================================================
// core/tests/streamSeek — behavioral regression test for #280 and #448: seeking a
// streamed Sound moves the audio, and a stream that cannot be read ends
// instead of spinning the StreamWorker thread.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The engine runs on miniaudio's null backend
// (AudioSettings::backend = AudioBackend::Null), a device-less clock that still
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
// - While a seek is pending (the worker has not served it yet), nothing of
//   the old position is heard, and a non-looping stream does not end at the
//   old data's end: it plays from the target once the seek is served.
// - A seek after an underrun at speed 10 (the mixer a few frames past the
//   written data) keeps the ring bounded: the worker goes on refilling the
//   other streams.
// - Eager sounds seek at once.
// - loadStream() rejects a file with no audio frames (DecodeFailed), and
//   accepts a FLAC whose length is unknown (STREAMINFO total 0), which plays
//   to its end.
// - A looping stream whose decoder returns no frames (the file was emptied
//   after loadStream()) ends with one error log, and the worker goes on
//   refilling the other streams.
// - A decoder read error, a failed loop seek and a failed seek request each
//   end the stream with one error log (a test hook makes the decoder fail).
//   Both looping and non-looping voices end. There is no automatic retry;
//   the app can reload and play again after isPlaying() becomes false.
// - The frames a failing read still returned are played before the voice
//   ends.
// - An MP3 stream's decoder gets a seek table (one point per second, at
//   most 1024), also when the re-init migration reopens it; a WAV's has
//   none.
// - A seek to getDuration() on a long file (the float duration a few frames
//   past the last frame) does not fail: a looping stream loops.
// - A stream whose length is unknown ignores setPosition(), with one
//   warning.
// - After the engine is re-initialized at another rate, getPosition()
//   carries over (exactly, for a stream paused across the re-init, so the
//   check does not depend on how long init() takes), also for a voice that
//   has ended (and its pending seek), and setPosition() lands at the target.
// - A voice that ended and whose slot another play() took, and a voice
//   whose decoder does not reopen at the re-init (a test hook), keep their
//   getPosition() across the re-init (the latter also a pending target).
// - The re-init migration clamps a pending target past the end of a long
//   MP3 (the seek does not fail), and a migration seek that fails (a test
//   hook) ends the stream with one error log.
// - A seek near the end plays every remaining frame before the voice ends,
//   also when the mixer read the ring's write position before the worker
//   wrote the tail (a test hook delays the mixer there).
// - A stream whose length is unknown restarts from the beginning at a
//   re-init, and getPosition() says so.
// - AudioEngine::getPlayingSounds() reports a stream's position as getPosition()
//   does, after a re-init at another rate and after shutdown() and an init
//   at another rate (no migration: the voice keeps its old rate).
// A watchdog turns a StreamWorker that never comes back into a FAIL.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcStreamSeekDiagnostics.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std;
using namespace tc;

namespace {

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

// Hold delivery BEFORE capture, after halt() has published endOfStream.
// The main thread releases this only after observing the voice end without
// its log. All state is global: an in-flight listener cannot outlive it.
static atomic<bool> g_holdReadFailLog{false};
static atomic<bool> g_readFailLogEntered{false};
static atomic<bool> g_readFailLogTimedOut{false};

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

// Minimal FLAC: 16-bit stereo, 48 kHz, fixed 4096-frame blocks of VERBATIM
// subframes at a DC `level`. STREAMINFO leaves the total sample count at 0
// ("unknown"), as an encoder writing to a pipe does. No blocks writes a file
// with no audio.
static bool writeFlacUnknownLength(const fs::path& path, int blocks, float level) {
    constexpr int kBlock = 4096;
    vector<uint8_t> out;
    auto put = [&](uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) out.push_back((uint8_t)(v >> (8 * i)));
    };
    out.insert(out.end(), {'f', 'L', 'a', 'C'});
    out.push_back(0x80);   // last metadata block, type 0 = STREAMINFO
    put(34, 3);
    put(kBlock, 2); put(kBlock, 2);   // min / max block size
    put(0, 3); put(0, 3);             // min / max frame size: unknown
    put(((uint64_t)kRate << 44) | ((uint64_t)(2 - 1) << 41) | ((uint64_t)(16 - 1) << 36), 8);
    out.insert(out.end(), 16, 0);     // MD5: not computed
    const int16_t v = (int16_t)lround(level * 32768.0f);
    for (int b = 0; b < blocks; ++b) {
        const size_t start = out.size();
        // Sync + fixed blocking, 4096 frames (0xC) at 48 kHz (0xA),
        // 2 independent channels (0x1), 16 bits (0x4), frame number < 128.
        out.insert(out.end(), {0xFF, 0xF8, 0xCA, 0x18, (uint8_t)b});
        uint8_t crc8 = 0;
        for (size_t i = start; i < out.size(); ++i) {
            crc8 ^= out[i];
            for (int k = 0; k < 8; ++k) crc8 = (uint8_t)((crc8 & 0x80) ? (crc8 << 1) ^ 0x07 : crc8 << 1);
        }
        out.push_back(crc8);
        for (int ch = 0; ch < 2; ++ch) {
            out.push_back(0x02);   // VERBATIM subframe
            for (int i = 0; i < kBlock; ++i) put((uint16_t)v, 2);
        }
        uint16_t crc16 = 0;
        for (size_t i = start; i < out.size(); ++i) {
            crc16 ^= (uint16_t)(out[i] << 8);
            for (int k = 0; k < 8; ++k)
                crc16 = (uint16_t)((crc16 & 0x8000) ? (crc16 << 1) ^ 0x8005 : crc16 << 1);
        }
        put(crc16, 2);
    }
    ofstream f(path, ios::binary | ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), (streamsize)out.size());
    return (bool)f;
}

// Silent MP3: MPEG-1 Layer III, 32 kbps, 48 kHz, stereo, no CRC, i.e.
// 96-byte frames of 1152 samples; a header followed by zeros (side info with
// no main data) decodes to silence. No Xing / LAME tag, so the decoder
// counts the frames for the length.
constexpr uint32_t kMp3FrameSamples = 1152;
static bool writeSilentMp3(const fs::path& path, uint32_t frames) {
    char frame[96] = {};
    frame[0] = (char)0xFF; frame[1] = (char)0xFB; frame[2] = (char)0x14; frame[3] = 0x00;
    ofstream f(path, ios::binary | ios::trunc);
    if (!f) return false;
    for (uint32_t i = 0; i < frames; ++i) f.write(frame, sizeof(frame));
    return (bool)f;
}

// Mean of the left channel over the last audioOut block.
static atomic<float> g_level{0.0f};

// Every audioOut block is numbered; while g_record is set, each block's mean
// is logged with its number.
static atomic<uint64_t> g_blocks{0};
static atomic<bool> g_record{false};
static mutex g_blockMutex;
static vector<pair<uint64_t, float>> g_blockLevels;
static double g_recordSum = 0.0;   // left-channel sum of the recorded blocks

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

static bool approx(float a, float b, float tol) { return fabs(a - b) <= tol; }

// Wait (up to `timeoutMs`) until `pred()` holds.
template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(timeoutMs);
    do {
        if (pred()) return true;
        sleepMs(5);
    } while (chrono::steady_clock::now() < deadline);
    return pred();
}

// Position of the (single) streamed voice as the engine plays it.
static float streamVoicePosition() {
    for (auto& v : AudioEngine::getInstance().getPlayingSounds()) {
        if (v.streaming) return v.position;
    }
    return -1.0f;
}

// Waits for a level the audio should reach (up to 2 s), so no check depends
// on how long a sleep takes.
static bool waitLevel(float level) {
    return waitFor([level] { return approx(g_level.load(), level, 0.02f); }, 2000);
}

static float secondsSince(chrono::steady_clock::time_point t0) {
    return chrono::duration<float>(chrono::steady_clock::now() - t0).count();
}

// How far playback moved on from `target`, which it started from no earlier
// than `t0`: wait (up to 2 s) until getPosition() is past target + 0.05 s,
// and measure the time since `t0`. The position may not be past the target
// by more than that time plus kAheadSec, a margin for the mixer running
// ahead of the clock. getPosition() never reads below the target meanwhile,
// so a voice that plays on from elsewhere does not pass by reaching the
// window later.
constexpr float kAheadSec = 0.05f;
struct PlayedFrom {
    float position = -1.0f;
    float lowest = 1e9f;  // lowest getPosition() during the wait
    float sec = 0.0f;     // seconds from t0 to the read
    bool moved = false;   // past target + 0.05 s within 2 s
    bool ok(float target) const {
        return moved && lowest >= target - 0.001f && position > target + 0.05f &&
               position <= target + sec + kAheadSec;
    }
    string detail() const {
        return to_string(position) + " (" + to_string(sec) +
               " s after the seek or resume, lowest " + to_string(lowest) + ")";
    }
};

static PlayedFrom playedFrom(Sound& s, float target, chrono::steady_clock::time_point t0) {
    PlayedFrom r;
    r.moved = waitFor([&] {
        const float q = s.getPosition();
        if (q < r.lowest) r.lowest = q;
        return q > target + 0.05f;
    }, 2000);
    r.position = s.getPosition();
    r.sec = secondsSince(t0);
    return r;
}

// Capture the level and reported position in audioOut, before the next mix
// can advance the voice. A Sound copy owns the observed voice; shared state
// also survives a callback already in flight when the listener disconnects.
struct SeekBlock {
    uint64_t frame = 0;
    int frames = 0;
    int targetFrames = 0; // includes a partial first block, excludes silence
    int rate = 0;
    float level = 0.0f;
    float position = -1.0f;
    internal::StreamSeekStateForTests seek;
};

struct SeekTrace {
    chrono::steady_clock::time_point t0;
    float first = -1.0f;
    float minSeen = 1e9f;
    float atSwitch = -1.0f;
    bool switched = false; // still requires a full block at the target level
    bool observerDelayed = false;
    SeekBlock firstAudio;
    string detail() const {
        const auto& b = firstAudio;
        return "position=" + to_string(atSwitch) + ", level=" + to_string(b.level) +
               ", frame=" + to_string(b.frame) + ", frames=" + to_string(b.frames) +
               ", targetFrames=" + to_string(b.targetFrames) +
               ", seek request/served/published/applied=" + to_string(b.seek.request) +
               "/" + to_string(b.seek.served) + "/" + to_string(b.seek.published) +
               "/" + to_string(b.seek.applied);
    }
    bool atTarget(float target) const {
        // Keep the original 0.1 s margin, and additionally check the actual
        // frames in the first audible block (including a partial block).
        return switched && atSwitch >= target - 0.001f && atSwitch < target + 0.1f &&
               firstAudio.rate > 0 &&
               approx(atSwitch, target + (float)firstAudio.targetFrames / firstAudio.rate, 0.001f);
    }
};

static SeekTrace seekAndTrace(Sound& s, float target, float newLevel, bool delayObserver = false) {
    struct Recording {
        mutex lock;
        bool armed = false;
        vector<SeekBlock> blocks;
    };
    auto recording = make_shared<Recording>();
    auto sub = AudioEngine::getInstance().audioOut.listen(
        [recording, voice = s, newLevel](AudioOutBuffer& b) {
            lock_guard<mutex> lock(recording->lock);
            if (!recording->armed) return;
            SeekBlock block;
            block.frame = b.framePosition;
            block.frames = b.frameCount;
            block.rate = b.sampleRate;
            double sum = 0.0;
            for (int i = 0; i < b.frameCount; ++i) {
                const float sample = b.data[i * b.channels];
                sum += sample;
                if (approx(sample, newLevel, 0.02f)) ++block.targetFrames;
            }
            block.level = b.frameCount > 0 ? (float)(sum / b.frameCount) : 0.0f;
            block.position = voice.getPosition();
            block.seek = internal::streamSeekStateForTests(voice);
            recording->blocks.push_back(block);
        });
    SeekTrace t;
    {
        lock_guard<mutex> lock(recording->lock);
        t.t0 = chrono::steady_clock::now();
        s.setPosition(target);
        t.first = s.getPosition();
        recording->armed = true;
    }
    if (delayObserver) {
        // Deliberately consume the first target level only AFTER later blocks
        // have advanced the position beyond the old 0.1 s window. No elapsed
        // sleep decides success: the recorded playback condition does.
        t.observerDelayed = waitFor([&] {
            lock_guard<mutex> lock(recording->lock);
            return !recording->blocks.empty() && recording->blocks.back().position > target + 0.15f;
        }, 2000);
    }
    waitFor([&] {
        const float p = s.getPosition();
        t.minSeen = min(t.minSeen, p);
        lock_guard<mutex> lock(recording->lock);
        for (const auto& b : recording->blocks) {
            t.minSeen = min(t.minSeen, b.position);
            // A callback mixed just before setPosition can still deliver the
            // old level; it cannot contain the new target's DC samples.
            if (b.targetFrames > 0 && t.atSwitch < 0.0f) {
                t.firstAudio = b;
                t.atSwitch = b.position;
            }
            if (approx(b.level, newLevel, 0.02f)) {
                t.switched = true;
                return true;
            }
        }
        return false;
    }, 2000);
    sub.disconnect();
    return t;
}

} // namespace

TC_CORE_TEST_MAIN() {
    // A StreamWorker stuck in one stream never returns, and its static
    // destructor would then hang the exit: fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout (the StreamWorker did not come back)\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    getMainThreadId();   // this thread is the main thread

    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (g_holdReadFailLog.load() && e.message.find("decoder read failed") != string::npos) {
            g_readFailLogEntered.store(true);
            if (!waitFor([] { return !g_holdReadFailLog.load(); }, 1000)) {
                g_readFailLogTimedOut.store(true);
            }
        }
        lock_guard<mutex> lock(g_logMutex);
        g_logs.push_back({e.level, e.message});
    });

    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
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
        const float level = b.frameCount > 0 ? (float)(sum / b.frameCount) : 0.0f;
        g_level.store(level);
        lock_guard<mutex> lock(g_blockMutex);
        const uint64_t n = g_blocks.fetch_add(1) + 1;
        if (g_record.load()) {
            g_blockLevels.push_back({n, level});
            g_recordSum += sum;
        }
    });

    const fs::path dir = fs::temp_directory_path() / ("tc_streamSeek_" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path dcWav = dir / "dc.wav";        // 0.1 for 0-1 s, 0.5 for 1-3 s
    const fs::path bgmWav = dir / "bgm.wav";      // 0.5 for 3 s
    const fs::path shortWav = dir / "short.wav";  // 0.1 for 0.2 s
    const fs::path emptyWav = dir / "empty.wav";  // no frames
    const fs::path vanishWav = dir / "vanish.wav";
    const fs::path negWav = dir / "neg.wav";      // -0.3 for 0-1 s, 0.5 for 1-3 s
    const fs::path tailWav = dir / "tail.wav";    // 0.5 for 0.1 s, 0.1 for 0.2 s
    const fs::path loopWav = dir / "loop.wav";    // silence, 9216 frames (9 x 1024)
    const fs::path unknownFlac = dir / "unknown.flac";   // 0.3, length unknown
    const fs::path emptyFlac = dir / "empty.flac";       // no frames, length unknown
    const fs::path shortMp3 = dir / "short.mp3";         // silent, 31.2 s
    const fs::path longMp3 = dir / "long.mp3";           // silent, ~18 min (see below)
    const fs::path endWav = dir / "end.wav";      // 0 for 11000 frames, 0.5 for 1000
    // A length whose float duration (what loadStream() stores) times the
    // rate, minus one, is past the last frame: Sound::setPosition(getDuration())
    // then targets a frame after the end (~18 minutes, ~4 MB).
    uint32_t longMp3Frames = 0;
    for (uint32_t n = 44500; n < 46000 && longMp3Frames == 0; ++n) {
        const uint64_t total = (uint64_t)n * kMp3FrameSamples;
        const float duration = (float)((double)total / (double)kRate);
        if ((uint64_t)((double)duration * kRate - 1.0) > total) longMp3Frames = n;
    }
    const uint64_t longMp3Total = (uint64_t)longMp3Frames * kMp3FrameSamples;
    check("test files are written",
          writeWav(dcWav, {{1.0f, 0.1f}, {2.0f, 0.5f}}) && writeWav(bgmWav, {{3.0f, 0.5f}}) &&
          writeWav(shortWav, {{0.2f, 0.1f}}) && writeWav(emptyWav, {}) &&
          writeWav(vanishWav, {{0.5f, 0.1f}}) &&
          writeWav(negWav, {{1.0f, -0.3f}, {2.0f, 0.5f}}) &&
          writeWav(tailWav, {{0.1f, 0.5f}, {0.2f, 0.1f}}) &&
          writeWav(loopWav, {{9216.0f / kRate, 0.0f}}) &&
          writeFlacUnknownLength(unknownFlac, 6, 0.3f) &&
          writeFlacUnknownLength(emptyFlac, 0, 0.3f) &&
          writeSilentMp3(shortMp3, 1300) && writeWav(endWav, {{11000.0f / kRate, 0.0f}, {1000.0f / kRate, 0.5f}}) &&
          longMp3Frames > 0 && writeSilentMp3(longMp3, longMp3Frames));

    // --- MP3 preparation is per load, shared across voices and engine rates (#463) ---
    {
        const auto scans = internal::streamMp3ScansForTests();
        const auto builds = internal::streamMp3TableBuildsForTests();
        auto preparedOnce = [&] {
            return internal::streamMp3ScansForTests() == scans + 1 &&
                   internal::streamMp3TableBuildsForTests() == builds + 1;
        };
        AudioSettings loadSettings = settings;
        loadSettings.sampleRate = 44100;
        check("MP3 cache: load at a non-native engine rate", engine.init(loadSettings));
        Sound m;
        check("MP3 cache: load the long file", (bool)m.loadStream(longMp3, 2));
        check("MP3 cache: load scans and builds once", preparedOnce());
        Sound other = m; // same source; play() creates independent voices
        for (int i = 0; i < 3; ++i) {
            check("MP3 cache: repeated play succeeds", m.play());
            check("MP3 cache: play does not scan or build", preparedOnce());
            m.stop();
        }
        check("MP3 cache: two simultaneous voices play", m.play() && other.play());
        const auto first = internal::streamSeekStateForTests(m);
        const auto second = internal::streamSeekStateForTests(other);
        check("MP3 cache: both voices bind the same table",
              first.mp3SeekTable && first.mp3SeekTable == second.mp3SeekTable);
        check("MP3 cache: initial length uses the engine rate",
              first.totalFrames == longMp3Total * 44100 / kRate);
        check("MP3 cache: simultaneous play does not scan or build", preparedOnce());
        m.setSpeed(0.0f);
        other.setSpeed(0.0f);
        for (int rate : {96000, 44100, kRate}) {
            AudioSettings changed = settings;
            changed.sampleRate = rate;
            check("MP3 cache: re-init at another rate", engine.init(changed));
            const auto migrated = internal::streamSeekStateForTests(m);
            const auto migratedOther = internal::streamSeekStateForTests(other);
            check("MP3 cache: re-init retains the same table for both voices",
                  migrated.mp3SeekTable == first.mp3SeekTable &&
                  migratedOther.mp3SeekTable == first.mp3SeekTable);
            // At these rates miniaudio truncates to whole output frames.
            check("MP3 cache: length converts from the native frame count",
                  migrated.totalFrames == longMp3Total * rate / kRate &&
                  migratedOther.totalFrames == migrated.totalFrames);
            check("MP3 cache: re-init does not scan or build", preparedOnce());
            for (float target : {900.0f, 100.0f, 600.0f}) {
                m.setPosition(target);
                const bool applied = waitFor([&] {
                    const auto state = internal::streamSeekStateForTests(m);
                    return state.request != 0 && state.applied == state.request;
                }, 2000);
                check("MP3 cache: long forward/backward seek is applied at target",
                      applied && m.isPlaying() && approx(m.getPosition(), target, 0.001f));
            }
        }
        m.stop();
        m = Sound{};
        other.setPosition(800.0f);
        check("MP3 cache: remaining voice can seek after the original is released",
              waitFor([&] {
                  const auto state = internal::streamSeekStateForTests(other);
                  return state.request != 0 && state.applied == state.request;
              }, 2000) && approx(other.getPosition(), 800.0f, 0.001f));
        check("MP3 cache: seeks do not scan or build", preparedOnce());
        other.stop();
    }

    // --- seek while playing ------------------------------------------------------
    Sound s;
    check("loadStream() opens the DC file", (bool)s.loadStream(dcWav) && s.isStreaming());
    check("the stream plays", s.play());
    check("a WAV stream's decoder has no seek table",
          internal::lastStreamSeekPointsForTests() == 0,
          to_string(internal::lastStreamSeekPointsForTests()));
    check("it plays from the start (level 0.1)", waitLevel(0.1f), to_string(g_level.load()));

    SeekTrace t = seekAndTrace(s, 2.0f, 0.5f);
    check("getPosition() is the target right after setPosition()", approx(t.first, 2.0f, 0.001f),
          to_string(t.first));
    check("the audio moved to the target (level 0.5)", t.switched, to_string(g_level.load()));
    check("getPosition() never reported the old position meanwhile", t.minSeen >= 2.0f - 0.001f,
          to_string(t.minSeen));
    check("getPosition() is still the target when the audio moves", t.atTarget(2.0f), t.detail());
    PlayedFrom pf = playedFrom(s, 2.0f, t.t0);
    check("then getPosition() follows the playback from the target", pf.ok(2.0f), pf.detail());

    // --- seek while paused -------------------------------------------------------
    s.pause();
    sleepMs(50);
    const float voiceBefore = streamVoicePosition();
    s.setPosition(0.25f);
    check("paused: getPosition() is the target at once", approx(s.getPosition(), 0.25f, 0.001f),
          to_string(s.getPosition()));
    sleepMs(100);
    check("paused: getPosition() stays at the target", approx(s.getPosition(), 0.25f, 0.001f),
          to_string(s.getPosition()));
    const float voiceAfter = streamVoicePosition();
    check("paused: the voice itself has not moved (the mixer applies the seek)",
          voiceBefore > 2.0f && approx(voiceAfter, voiceBefore, 0.001f),
          to_string(voiceBefore) + " -> " + to_string(voiceAfter));
    auto t0 = chrono::steady_clock::now();
    s.resume();
    check("resumed: it plays from the target (level 0.1)", waitLevel(0.1f),
          to_string(g_level.load()));
    pf = playedFrom(s, 0.25f, t0);
    check("resumed: getPosition() follows from the target", pf.ok(0.25f), pf.detail());

    // --- repeated seeks: the last one wins ---------------------------------------
    s.setPosition(2.5f);
    s.setPosition(0.2f);
    t0 = chrono::steady_clock::now();
    s.setPosition(1.8f);
    check("repeated: getPosition() is the last target", approx(s.getPosition(), 1.8f, 0.001f),
          to_string(s.getPosition()));
    check("repeated: the audio is at the last target (level 0.5)", waitLevel(0.5f),
          to_string(g_level.load()));
    pf = playedFrom(s, 1.8f, t0);
    check("repeated: getPosition() follows from the last target", pf.ok(1.8f), pf.detail());

    // Paused: the worker serves the first request while the second waits for
    // the mixer, which only runs again on resume.
    s.pause();
    s.setPosition(2.6f);
    sleepMs(50);
    s.setPosition(0.4f);
    check("repeated while paused: getPosition() is the last target",
          approx(s.getPosition(), 0.4f, 0.001f), to_string(s.getPosition()));
    t0 = chrono::steady_clock::now();
    s.resume();
    check("repeated while paused: the audio is at the last target (level 0.1)", waitLevel(0.1f),
          to_string(g_level.load()));
    pf = playedFrom(s, 0.4f, t0);
    check("repeated while paused: getPosition() follows from the last target", pf.ok(0.4f),
          pf.detail());
    s.stop();

    // Reproduce the level-before-position observation order under a delayed
    // observer. The original seek checks also run without the injected delay.
    {
        Sound delayed;
        check("delayed observer: the old level plays",
              (bool)delayed.loadStream(dcWav) && delayed.play() && waitLevel(0.1f));
        const SeekTrace trace = seekAndTrace(delayed, 2.0f, 0.5f, true);
        check("delayed observer: later playback precedes reading the first target block",
              trace.observerDelayed, trace.detail());
        check("delayed observer: immediate target and no old position",
              approx(trace.first, 2.0f, 0.001f) && trace.minSeen >= 2.0f - 0.001f, trace.detail());
        check("delayed observer: target audio and position agree in their block",
              trace.atTarget(2.0f), trace.detail());
        const PlayedFrom delayedPlayback = playedFrom(delayed, 2.0f, trace.t0);
        check("delayed observer: position follows playback",
              delayedPlayback.ok(2.0f), delayedPlayback.detail());
        delayed.stop();
    }

    // --- while a seek is pending ---------------------------------------------------
    // The worker is held back (Stalls) so the request stays pending while the
    // ring still holds the old position's data.
    {
        Sound g;
        check("pending: a stream at level -0.3 plays",
              (bool)g.loadStream(negWav) && g.play() && waitLevel(-0.3f), to_string(g_level.load()));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
        {
            lock_guard<mutex> lock(g_blockMutex);
            g_blockLevels.clear();
        }
        g_record.store(true);
        g.setPosition(2.0f);
        const uint64_t called = g_blocks.load();
        sleepMs(60);   // the worker serves nothing meanwhile
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        // Until the audio is at the target, and at least 11 blocks after the
        // one that may predate the request.
        const bool served = waitFor([&] {
            return approx(g_level.load(), 0.5f, 0.02f) && g_blocks.load() > called + 12;
        }, 2000);
        g_record.store(false);
        // One block may have been mixed before the request (its audioOut can
        // follow the call); every later block holds no old audio.
        int oldBlocks = 0, blocks = 0;
        {
            lock_guard<mutex> lock(g_blockMutex);
            for (auto& b : g_blockLevels) {
                if (b.first <= called + 1) continue;
                ++blocks;
                if (b.second < -0.01f) ++oldBlocks;
            }
        }
        check("pending: nothing of the old position is heard after setPosition()",
              blocks > 10 && oldBlocks == 0,
              to_string(oldBlocks) + " of " + to_string(blocks) + " blocks at the old level");
        check("pending: once served, the audio is at the target (level 0.5)", served,
              to_string(g_level.load()));
        g.stop();

        Sound n;
        check("pending: a short non-looping stream plays", (bool)n.loadStream(tailWav) && n.play());
        // The whole file is in the ring after 50 ms: the worker has hit its
        // end. Nothing signals that, so a sleep that overruns past the 0.3 s
        // file would let it end before setPosition(): check and retry.
        bool early = false;
        string earlyDetail;
        for (int attempt = 1; attempt <= 3 && !early; ++attempt) {
            if (attempt > 1) {
                n.stop();
                n.play();
            }
            sleepMs(50);
            bool playing = n.isPlaying();
            float pos = n.getPosition();
            early = playing && pos < 0.2f;
            earlyDetail = to_string(attempt) + " attempts, last: position " + to_string(pos) +
                          ", isPlaying " + (playing ? "true" : "false");
        }
        check("pending: it is still playing early in the file (position < 0.2)", early,
              early ? earlyDetail : earlyDetail + ", " + tcCoreTest::streamSeekFailureState(n, g_level.load()));
        if (early) {
            internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
            n.setPosition(0.0f);
            const uint64_t pendingAt = g_blocks.load();
            // Process 0.4 s of callbacks, longer than what the ring held.
            const bool pending = waitFor([&] {
                return g_blocks.load() - pendingAt >= kRate * 4 / 10 / settings.bufferSize;
            }, 2000);
            const bool playing = pending && n.isPlaying();
            check("pending: a non-looping stream does not end at the old data's end",
                  playing, playing ? "" : tcCoreTest::streamSeekFailureState(n, g_level.load()));
            {
                lock_guard<mutex> lock(g_blockMutex);
                g_blockLevels.clear();
                g_record.store(true);
                // Arm recording before the worker can produce target audio.
                internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
            }
            bool ended = false;
            uint64_t endedAt = 0;
            waitFor([&] {
                if (!ended && !n.isPlaying()) {
                    ended = true;
                    endedAt = g_blocks.load();
                }
                // The mixer can end the voice before audioOut records its
                // final block. Wait for a callback after observing the end.
                return ended && g_blocks.load() > endedAt;
            }, 2000);
            bool heard = false;
            {
                lock_guard<mutex> lock(g_blockMutex);
                g_record.store(false);
                for (const auto& b : g_blockLevels) {
                    if (approx(b.second, 0.5f, 0.02f)) heard = true;
                }
            }
            check("pending: then it plays from the target (level 0.5)",
                  heard, heard ? "" : tcCoreTest::streamSeekFailureState(n, g_level.load()));
        }
        n.stop();
    }

    // --- eager sounds seek at once -----------------------------------------------
    Sound eager;
    check("the DC file loads eagerly", (bool)eager.load(dcWav) && !eager.isStreaming());
    check("eager: it plays (level 0.1)", eager.play() && waitLevel(0.1f), to_string(g_level.load()));
    {
        // The voice plays on between the two calls (a slow runner may lose
        // the thread there): from the target, not the old position, by at
        // most the time between them plus a margin for the mixer running ahead.
        const auto t0 = chrono::steady_clock::now();
        eager.setPosition(2.0f);
        const float p = eager.getPosition();
        const float sec = chrono::duration<float>(chrono::steady_clock::now() - t0).count();
        check("eager: getPosition() is the target at once", p >= 2.0f && p - 2.0f <= sec + 0.05f,
              to_string(p) + " (" + to_string(sec) + " s between the calls)");
    }
    // The file reaches 0.5 by itself after 1 s: the voice is past the target
    // when it is heard.
    {
        const bool heard = waitLevel(0.5f);
        const float p = eager.getPosition();
        check("eager: the audio moved (level 0.5)", heard && p >= 2.0f,
              to_string(g_level.load()) + " at " + to_string(p));
    }
    eager.stop();

    // --- zero-length file --------------------------------------------------------
    {
        Sound z;
        const LoadResult r = z.loadStream(emptyWav);
        check("loadStream() of a file with no frames fails with DecodeFailed",
              !r && r.error == LoadError::DecodeFailed && !z.isLoaded(), loadErrorName(r.error));
        check("the empty file is logged", countLogs(LogLevel::Error, "no audio frames") == 1,
              lastLog(LogLevel::Error));

        Sound ef;
        const LoadResult re = ef.loadStream(emptyFlac);
        check("loadStream() of a FLAC with no frames and an unknown length fails",
              !re && re.error == LoadError::DecodeFailed, loadErrorName(re.error));
    }

    // --- a FLAC whose length is unknown --------------------------------------------
    {
        Sound u;
        const LoadResult r = u.loadStream(unknownFlac);
        check("loadStream() accepts a FLAC whose length is unknown", (bool)r, r.message);
        check("its duration is 0 (unknown)", u.getDuration() == 0.0f, to_string(u.getDuration()));
        check("it plays (level 0.3)",
              u.play() && waitLevel(0.3f), to_string(g_level.load()));
        // With no known end there is nothing to clamp a target to: the
        // seek is refused (it used to jump to the start).
        waitFor([&] { return u.getPosition() > 0.05f; }, 2000);
        const size_t warned = countLogs(LogLevel::Warning, "length is unknown");
        const float before = u.getPosition();
        u.setPosition(0.4f);
        const float after = u.getPosition();
        check("unknown length: setPosition() is ignored (getPosition() goes on)",
              before > 0.05f && after >= before, to_string(before) + " -> " + to_string(after));
        // getPosition() would report an accepted target at once: it never
        // drops below `after`, and goes past it.
        u.setPosition(0.0f);
        float lowest = u.getPosition();
        const bool movedOn = waitFor([&] {
            const float q = u.getPosition();
            if (q < lowest) lowest = q;
            return q > after;
        }, 2000);
        check("unknown length: the audio did not jump (getPosition() still goes on)",
              movedOn && lowest >= after, to_string(lowest) + " -> " + to_string(u.getPosition()));
        check("unknown length: the refused seek is logged once",
              countLogs(LogLevel::Warning, "length is unknown") == warned + 1,
              lastLog(LogLevel::Warning));
        check("it ends at the end of the file", waitFor([&] { return !u.isPlaying(); }, 2000));
        u.stop();
    }

    // --- a looping stream emptied after loading: ends, the others play on ---------
    {
        Sound bgm, vanish;
        // Looping, so it plays on however long the wait below takes.
        check("a second stream plays",
              (bool)bgm.loadStream(bgmWav) && (bgm.setLoop(true), bgm.play()));
        check("the stream to empty loads", (bool)vanish.loadStream(vanishWav));
        // The file goes empty (a failed recording overwritten, storage gone
        // quiet): the voice's own decoder opens it with no frames.
        check("the file is emptied after loadStream()", writeWav(vanishWav, {}));
        vanish.setLoop(true);
        const size_t before = countLogs(LogLevel::Error, "no frames to read");
        check("the emptied stream starts", vanish.play());
        check("the emptied looping stream ends with an error log",
              waitFor([&] { return countLogs(LogLevel::Error, "no frames to read") == before + 1; },
                      1000),
              lastLog(LogLevel::Error));
        check("the emptied looping voice stops",
              waitFor([&] { return !vanish.isPlaying(); }, 1000));
        // Advance beyond the other stream's ring capacity, so a stuck
        // worker cannot pass by playing only already buffered frames.
        check("the other stream is still refilled beyond its initial ring",
              waitFor([&] { return bgm.getPosition() > 1.0f; }, 2000) && waitLevel(0.5f),
              to_string(g_level.load()));
        check("the emptied looping voice stays stopped", !vanish.isPlaying());
        check("the error is logged once",
              countLogs(LogLevel::Error, "no frames to read") == before + 1);
        vanish.stop();
        bgm.stop();
    }

    // --- decoder read error ------------------------------------------------------
    {
        Sound c;
        check("a looping stream plays",
              (bool)c.loadStream(dcWav) && (c.setLoop(true), c.play()) && waitLevel(0.1f),
              to_string(g_level.load()));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::ReadFails);
        check("a read error ends the stream with an error log",
              waitFor([] { return countLogs(LogLevel::Error, "decoder read failed") == 1; }, 1000),
              lastLog(LogLevel::Error));
        // The ring drains; nothing new is decoded.
        const bool silent = waitFor([] { return approx(g_level.load(), 0.0f, 0.001f); }, 2000);
        check("the read error is logged once",
              countLogs(LogLevel::Error, "decoder read failed") == 1);
        check("after a read error the voice falls silent", silent, to_string(g_level.load()));
        check("after a read error the looping voice stops",
              waitFor([&] { return !c.isPlaying(); }, 1000));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        const uint64_t stoppedAt = g_blocks.load();
        check("audio callbacks continue after the fault is cleared",
              waitFor([&] { return g_blocks.load() > stoppedAt + 4; }, 1000));
        check("clearing the read fault does not retry or log again",
              !c.isPlaying() && engine.getPlayingSounds().empty() &&
              countLogs(LogLevel::Error, "decoder read failed") == 1);
        check("the app can reload and play after the read error",
              (bool)c.loadStream(bgmWav) && c.play() && waitLevel(0.5f), to_string(g_level.load()));
        c.stop();

        Sound after;
        check("the worker refills a new stream afterwards", (bool)after.loadStream(bgmWav) &&
              after.play() && waitLevel(0.5f),
              to_string(g_level.load()));
        after.stop();
    }

    // --- failed seek at the loop point -------------------------------------------
    {
        Sound e;
        check("a short looping stream plays",
              (bool)e.loadStream(shortWav) && (e.setLoop(true), e.play()) && waitLevel(0.1f),
              to_string(g_level.load()));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::SeekFails);
        check("a failed loop seek ends the stream with an error log",
              waitFor([] { return countLogs(LogLevel::Error, "seek to the start for the loop failed") == 1; },
                      1000),
              lastLog(LogLevel::Error));
        // The ring (~340 ms) drains; nothing new is decoded.
        const bool silent = waitFor([] { return approx(g_level.load(), 0.0f, 0.001f); }, 2000);
        check("the failed loop seek is logged once",
              countLogs(LogLevel::Error, "seek to the start for the loop failed") == 1);
        check("after a failed loop seek the voice falls silent", silent, to_string(g_level.load()));
        check("after a failed loop seek the looping voice stops",
              waitFor([&] { return !e.isPlaying(); }, 1000));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        check("the app can reload and play after the failed loop seek",
              (bool)e.loadStream(shortWav) && e.play() && waitLevel(0.1f), to_string(g_level.load()));
        e.stop();
    }

    // --- failed seek request -----------------------------------------------------
    for (bool loop : {false, true}) {
        Sound f;
        const size_t logged = countLogs(LogLevel::Error, "seek to frame");
        f.setLoop(loop);
        check("a stream plays", (bool)f.loadStream(dcWav) && f.play() && waitLevel(0.1f),
              to_string(g_level.load()));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::SeekFails);
        f.setPosition(2.0f);
        check("a failed seek request ends the stream with an error log",
              waitFor([&] { return countLogs(LogLevel::Error, "seek to frame") == logged + 1; }, 1000),
              lastLog(LogLevel::Error));
        check("after a failed seek the voice ends", waitFor([&] { return !f.isPlaying(); }, 1000));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        f.stop();
    }

    // --- a failing read that still returned frames ------------------------------------
    // The stream underruns (the worker held back), then the next read returns
    // 1024 frames and an error. Those frames must be published before the
    // stream ends: a mixer that saw the end first would end the voice
    // without them. Hold the worker before log capture until the test has
    // observed the end, deliberately reproducing the macOS failure order.
    for (bool loop : {false, true}) {
        Sound w;
        w.setLoop(loop);
        check("read error with frames: a stream at level 0.5 plays",
              (bool)w.loadStream(bgmWav) && w.play() && waitLevel(0.5f), to_string(g_level.load()));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
        // The ring (~340 ms) drains: the mixer underruns.
        const bool underran = waitFor([] { return approx(g_level.load(), 0.0f, 0.001f); }, 2000);
        check("read error with frames: the voice underruns, still playing",
              underran && w.isPlaying(), to_string(g_level.load()));
        {
            lock_guard<mutex> lock(g_blockMutex);
            g_blockLevels.clear();
        }
        const size_t logged = countLogs(LogLevel::Error, "decoder read failed");
        g_readFailLogEntered.store(false);
        g_readFailLogTimedOut.store(false);
        g_holdReadFailLog.store(true);
        g_record.store(true);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::ReadFailsWithFrames);
        const bool endBeforeLog = waitFor([&] {
            return !w.isPlaying() && g_readFailLogEntered.load();
        }, 1000);
        check("read error with frames: end is observed before log delivery",
              endBeforeLog && countLogs(LogLevel::Error, "decoder read failed") == logged &&
              !g_readFailLogTimedOut.load());
        g_holdReadFailLog.store(false);
        const bool endedAndLogged = waitFor([&] {
            return !w.isPlaying() && countLogs(LogLevel::Error, "decoder read failed") == logged + 1;
        }, 1000);
        g_record.store(false);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        int heard = 0;
        {
            lock_guard<mutex> lock(g_blockMutex);
            for (auto& b : g_blockLevels) {
                if (approx(b.second, 0.5f, 0.02f)) ++heard;
            }
        }
        check("read error with frames: the stream ends with an error log",
              endedAndLogged && countLogs(LogLevel::Error, "decoder read failed") == logged + 1,
              string(endedAndLogged ? "" : "timeout: ") + "playing=" + to_string(w.isPlaying()) +
              ", logs=" + to_string(countLogs(LogLevel::Error, "decoder read failed") - logged) +
              ", heard=" + to_string(heard) + " blocks; " + lastLog(LogLevel::Error));
        // 1024 frames = 4 blocks of 256 (a partial block reads lower).
        check("read error with frames: the frames it returned are played first",
              heard >= 3, to_string(heard) + " blocks at level 0.5");
        w.stop();
    }

    // --- MP3: seek table, and a seek to getDuration() on a long file ----------------------
    {
        Sound m;
        check("MP3: the 31.2 s file streams", (bool)m.loadStream(shortMp3) && m.play());
        check("MP3: its decoder has a seek table, one point per second (32)",
              internal::lastStreamSeekPointsForTests() == 32,
              to_string(internal::lastStreamSeekPointsForTests()));
        m.stop();

        // getDuration() is a float: on this ~18 minute file,
        // setPosition(getDuration()) targets a frame past the last one, and a
        // seek there fails in dr_mp3. The worker clamps it to the last frame,
        // so a looping stream loops instead of halting.
        Sound l;
        check("MP3 long: the file streams, looping",
              (bool)l.loadStream(longMp3) && (l.setLoop(true), l.play()));
        check("MP3 long: its seek table is capped at 1024 points",
              internal::lastStreamSeekPointsForTests() == 1024,
              to_string(internal::lastStreamSeekPointsForTests()));
        const double target = (double)l.getDuration() * kRate - 1.0;
        check("MP3 long: setPosition(getDuration()) targets a frame past the last one",
              (uint64_t)target > longMp3Total,
              to_string((uint64_t)target) + " vs " + to_string(longMp3Total) + " frames");
        waitFor([&] { return l.getPosition() > 0.05f; }, 2000);
        const size_t failed = countLogs(LogLevel::Error, "seek to frame");
        l.setPosition(l.getDuration());
        // getPosition() reports the target until the audio moves there; then
        // the voice loops back to the start.
        const bool looped = waitFor([&] { return l.getPosition() < 1.0f; }, 2000);
        check("MP3 long: the seek to the end does not fail",
              countLogs(LogLevel::Error, "seek to frame") == failed, lastLog(LogLevel::Error));
        const float p = l.getPosition();
        check("MP3 long: it looped to the start", looped && l.isPlaying() && p < 1.0f, to_string(p));
        l.stop();
    }

    // --- seek after an underrun at speed 10 ------------------------------------------
    // At speed 10 the mixer can end up to 8 frames past the written data when
    // the worker falls behind. setPosition(getDuration()) leaves one frame
    // before the loop point, so the refill after the seek is one frame off the
    // 1024-frame chunks and can fill the whole ring plus a frame, bounded by
    // the mixer's read position. If the mixer then moved back to the seek's
    // base frame, the ring would hold more than it can. The voice is frozen
    // (speed 0) before the seek, so the mixer does not read the excess away:
    // the worker would decode this stream forever and the other stream would
    // starve. Whether the mixer overshoots depends on where the worker
    // stalled, so try a few times.
    {
        Sound bg;
        check("overshoot: a looping stream at level 0.5 plays",
              (bool)bg.loadStream(bgmWav) && (bg.setLoop(true), bg.play()));
        int starved = 0;
        for (int trial = 0; trial < 4; ++trial) {
            Sound o;
            if (!o.loadStream(loopWav)) { ++starved; break; }
            o.setLoop(true);
            o.setSpeed(10.0f);
            o.play();
            sleepMs(50);
            internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
            sleepMs(80);   // o's ring (34 ms at speed 10) drains; bg's holds ~340 ms
            o.setSpeed(0.0f);
            o.setPosition(o.getDuration());
            internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
            sleepMs(600);  // longer than bg's ring
            if (!waitLevel(0.5f)) ++starved;
            o.stop();
        }
        check("overshoot: after a seek at speed 10 the other stream is still refilled",
              starved == 0, to_string(starved) + " of 4 trials starved");
        bg.stop();
    }

    // --- re-init at another rate ------------------------------------------------------
    {
        // r plays first, so the ended voices below do not take its slot
        // (play() reuses the first slot that is not playing).
        Sound r;
        check("re-init: a stream plays", (bool)r.loadStream(dcWav) && r.play());
        // Two voices that have ended, one of them with a seek the mixer never
        // applies (it no longer runs the voice). Their slots stay in the
        // engine, and getPosition() still reads them.
        Sound ended, endedSeek;
        check("re-init: two short streams play to their end",
              (bool)ended.loadStream(shortWav) && (bool)endedSeek.loadStream(shortWav) &&
              ended.play() && endedSeek.play() &&
              waitFor([&] { return !ended.isPlaying() && !endedSeek.isPlaying(); }, 1000));
        endedSeek.setPosition(0.1f);
        const float endedBefore = ended.getPosition();
        const float endedSeekBefore = endedSeek.getPosition();
        waitFor([&] { return r.getPosition() > 0.25f; }, 2000);
        // Paused across the re-init, so the position cannot move however long
        // init() takes: it carries over exactly. The migration rebuilds a
        // paused voice like a playing one. The mixer reads `paused` under the
        // engine lock that getPosition() takes, so `before` is final.
        r.pause();
        const float before = r.getPosition();
        AudioSettings s96 = settings;
        s96.sampleRate = 96000;
        check("re-init: the engine restarts at 96 kHz", engine.init(s96));
        const float after = r.getPosition();
        check("re-init: getPosition() carries over", before > 0.2f && approx(after, before, 0.001f),
              to_string(before) + " -> " + to_string(after));
        r.resume();
        check("re-init: an ended voice's getPosition() carries over",
              endedBefore > 0.15f && approx(ended.getPosition(), endedBefore, 0.001f),
              to_string(endedBefore) + " -> " + to_string(ended.getPosition()));
        check("re-init: an ended voice's pending seek target carries over",
              approx(endedSeekBefore, 0.1f, 0.001f) && approx(endedSeek.getPosition(), 0.1f, 0.001f),
              to_string(endedSeekBefore) + " -> " + to_string(endedSeek.getPosition()));
        const auto t0 = chrono::steady_clock::now();
        r.setPosition(1.5f);
        check("re-init: setPosition() lands at the target (level 0.5)", waitLevel(0.5f),
              to_string(g_level.load()));
        const PlayedFrom pf = playedFrom(r, 1.5f, t0);
        check("re-init: getPosition() follows from the target", pf.ok(1.5f), pf.detail());
        r.stop();
    }

    // --- re-init with an MP3 voice: the migrated decoder gets a seek table ----------------
    {
        Sound m, w;
        check("re-init MP3: an MP3 stream plays", (bool)m.loadStream(shortMp3) && m.play());
        // Open (and drop) a WAV voice, so the hook reads 0 until the
        // migration reopens the MP3's decoder.
        check("re-init MP3: a WAV voice opens", (bool)w.loadStream(bgmWav) && w.play());
        w.stop();
        check("re-init MP3: the hook reads the WAV decoder (0)",
              internal::lastStreamSeekPointsForTests() == 0);
        AudioSettings s441 = settings;
        s441.sampleRate = 44100;
        check("re-init MP3: the engine restarts at 44.1 kHz", engine.init(s441));
        check("re-init MP3: the migrated decoder has its seek table (32 points)",
              m.isPlaying() && internal::lastStreamSeekPointsForTests() == 32,
              to_string(internal::lastStreamSeekPointsForTests()));
        m.stop();
    }

    // The engine rate to re-init at next: any rate but the current one.
    auto otherRate = [&] { return engine.getSampleRate() == 96000 ? 48000 : 96000; };
    auto reinitAt = [&](int rate) {
        AudioSettings a = settings;
        a.sampleRate = rate;
        return engine.init(a);
    };
    check("back to 48 kHz", reinitAt(kRate));

    // --- a seek near the end plays the whole tail ------------------------------------
    // The mixer reads how far the ring is written at the top of its callback.
    // The file is shorter than the ring, so the worker has written all of it
    // and there is room for the tail after a seek. A seek to 1000 frames
    // before the end: the worker publishes the seek, writes those frames and
    // marks the end. MixerLags holds the worker until the mixer has read the
    // write position with the seek pending, and makes the mixer wait for all
    // that right after its read, so it moves to the seek and meets the end
    // with the tail past the write position it read: it must still play it.
    {
        Sound e;
        check("near end: a stream plays (silent part)",
              (bool)e.loadStream(endWav) && e.play() &&
              waitFor([&] { return e.getPosition() > 0.02f; }, 2000));
        {
            lock_guard<mutex> lock(g_blockMutex);
            g_blockLevels.clear();
            g_recordSum = 0.0;
        }
        g_record.store(true);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::MixerLags);
        e.setPosition(11000.0f / kRate);
        const bool ended = waitFor([&] { return !e.isPlaying(); }, 3000);
        g_record.store(false);
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        double sum;
        {
            lock_guard<mutex> lock(g_blockMutex);
            sum = g_recordSum;
        }
        // Every frame of the tail is 0.5 (the last one only feeds the
        // interpolation of the one before it).
        const double frames = sum / 0.5;
        check("near end: the voice ends", ended);
        check("near end: every remaining frame is played before it ends (999 of 1000)",
              frames > 990.0 && frames < 1002.0, to_string(frames) + " frames at 0.5");
        e.stop();
    }

    // --- the re-init migration clamps a pending target past the end ------------------
    // The pending target of setPosition(getDuration()) on the long MP3 is past
    // its last frame at 48 kHz, and so at 96 kHz. The migration seeks the new
    // decoder there itself; it clamps like the worker.
    {
        Sound l;
        check("re-init clamp: the long MP3 streams, looping",
              (bool)l.loadStream(longMp3) && (l.setLoop(true), l.play()) &&
              waitFor([&] { return l.getPosition() > 0.05f; }, 2000));
        l.pause();   // the seek stays pending: a paused voice does not apply it
        l.setPosition(l.getDuration());
        const size_t failed = countLogs(LogLevel::Error, "seek to frame");
        check("re-init clamp: the engine restarts at 96 kHz", reinitAt(96000));
        check("re-init clamp: the migration's seek to the end does not fail",
              countLogs(LogLevel::Error, "seek to frame") == failed, lastLog(LogLevel::Error));
        l.resume();
        const bool looped = waitFor([&] { return l.getPosition() < 1.0f; }, 2000);
        const float p = l.getPosition();
        check("re-init clamp: it looped to the start", looped && l.isPlaying() && p < 1.0f,
              to_string(p));
        l.stop();

        // A failed migration seek halts the stream, like a failed seek
        // request: one error log, and a non-looping voice ends.
        Sound f;
        check("re-init seek fails: a stream plays",
              (bool)f.loadStream(dcWav) && f.play() &&
              waitFor([&] { return f.getPosition() > 0.1f; }, 2000));
        internal::setStreamFaultForTests(internal::StreamFaultForTests::SeekFails);
        const size_t logged = countLogs(LogLevel::Error, "failed after an engine re-init");
        const bool restarted = reinitAt(otherRate());
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        check("re-init seek fails: the engine restarts", restarted);
        check("re-init seek fails: one error log",
              countLogs(LogLevel::Error, "failed after an engine re-init") == logged + 1,
              lastLog(LogLevel::Error));
        check("re-init seek fails: the voice ends", waitFor([&] { return !f.isPlaying(); }, 1000));
        f.stop();
    }

    // --- an ended voice whose slot another play() took --------------------------------
    // The migration only sees the engine's slots. A voice pushed out of them
    // keeps its position at the rate it counts it in.
    {
        Sound ended;
        check("slot reused: a short stream plays to its end",
              (bool)ended.loadStream(shortWav) && ended.play() &&
              waitFor([&] { return !ended.isPlaying(); }, 1000));
        const float before = ended.getPosition();
        // Eight eager voices take all eight slots, the ended voice's too.
        vector<Sound> fill(8);
        bool allPlay = true;
        for (auto& f : fill) allPlay = allPlay && (bool)f.load(shortWav) && f.play();
        check("slot reused: eight voices take every slot", allPlay);
        for (auto& f : fill) f.stop();
        check("slot reused: the engine restarts at another rate", reinitAt(otherRate()));
        check("slot reused: the ended voice's getPosition() carries over",
              before > 0.15f && approx(ended.getPosition(), before, 0.001f),
              to_string(before) + " -> " + to_string(ended.getPosition()));
    }

    // --- a voice whose decoder does not reopen at the re-init ------------------------------
    {
        Sound a, b;
        check("reopen fails: two streams play",
              (bool)a.loadStream(dcWav) && a.play() && (bool)b.loadStream(dcWav) && b.play() &&
              waitFor([&] { return a.getPosition() > 0.25f && b.getPosition() > 0.0f; }, 2000));
        // b holds a seek that stays pending (paused).
        b.pause();
        b.setPosition(2.5f);
        // a stays playing (the check below is that a playing voice ends), so
        // it plays on until init() stops the device: bound the gain by the
        // time from reading the position to the end of init(), plus a margin
        // for the mixer running ahead.
        const auto t0 = chrono::steady_clock::now();
        const float before = a.getPosition();
        const size_t warned = countLogs(LogLevel::Warning, "stream playback migration failed");
        const auto statsBefore = engine.getStats();
        internal::setStreamFaultForTests(internal::StreamFaultForTests::ReopenFails);
        const bool restarted = reinitAt(otherRate());
        const float elapsedSec = chrono::duration<float>(chrono::steady_clock::now() - t0).count();
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
        check("reopen fails: the engine restarts", restarted);
        check("reopen fails: both voices end with a warning",
              !a.isPlaying() && !b.isPlaying() &&
              engine.getStats().voicesStoppedByReinit == statsBefore.voicesStoppedByReinit + 2 &&
              engine.getStats().droppedPlays == statsBefore.droppedPlays &&
              countLogs(LogLevel::Warning, "stream playback migration failed") == warned + 2,
              lastLog(LogLevel::Warning));
        const float after = a.getPosition();
        check("reopen fails: getPosition() carries over",
              before > 0.2f && after >= before && after - before <= elapsedSec + 0.05f,
              to_string(before) + " -> " + to_string(after) + " (" +
                  to_string(elapsedSec) + " s from the read to the end of init())");
        check("reopen fails: a pending target carries over",
              approx(b.getPosition(), 2.5f, 0.001f), to_string(b.getPosition()));
        a.stop();
        b.stop();
    }

    // --- a stream whose length is unknown restarts at a re-init --------------------------
    {
        Sound u;
        check("unknown length re-init: the FLAC plays (level 0.3)",
              (bool)u.loadStream(unknownFlac) && u.play() &&
              waitLevel(0.3f) && waitFor([&] { return u.getPosition() > 0.2f; }, 2000));
        // Paused across the re-init, so the new decoder does not play on from
        // 0 while init() finishes: getPosition() is 0 exactly, however long
        // init() takes.
        u.pause();
        const float before = u.getPosition();
        check("unknown length re-init: the engine restarts at another rate", reinitAt(otherRate()));
        const float after = u.getPosition();
        u.resume();
        check("unknown length re-init: getPosition() restarts from 0 with the audio",
              before > 0.15f && after < 0.001f, to_string(before) + " -> " + to_string(after));
        check("unknown length re-init: it plays again (level 0.3)", waitLevel(0.3f),
              to_string(g_level.load()));
        check("unknown length re-init: then it ends", waitFor([&] { return !u.isPlaying(); }, 2000));
        u.stop();
    }

    // --- getPlayingSounds() reports a stream's position at the voice's own rate ------------------
    // It divides positionF by the rate the voice counts (as getPosition()
    // does), not the engine's current rate. The re-init migration brings a
    // playing voice to the new rate; an init after shutdown() does not
    // migrate, so a voice left over keeps the old one.
    {
        Sound v;
        check("getPlayingSounds(): a stream plays",
              (bool)v.loadStream(dcWav) && v.play() &&
              waitFor([&] { return v.getPosition() > 0.25f; }, 2000));
        check("getPlayingSounds(): the engine restarts at another rate", reinitAt(otherRate()));
        v.pause();
        float mine = v.getPosition(), listed = streamVoicePosition();
        check("getPlayingSounds(): after a re-init it agrees with getPosition()",
              mine > 0.2f && approx(listed, mine, 0.001f),
              to_string(listed) + " vs " + to_string(mine));
        engine.shutdown();
        check("getPlayingSounds(): the engine inits at another rate after shutdown()",
              reinitAt(otherRate()));
        mine = v.getPosition();
        listed = streamVoicePosition();
        check("getPlayingSounds(): after shutdown() and an init at another rate it agrees with "
              "getPosition()",
              mine > 0.2f && approx(listed, mine, 0.001f),
              to_string(listed) + " vs " + to_string(mine));
        v.stop();
    }

    levelSub.disconnect();   // the listener only touches globals: no barrier needed
    engine.shutdown();
    fs::remove_all(dir, ec);
    logSub.disconnect();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
