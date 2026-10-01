// =============================================================================
// core/tests/streamWorkerIdle — regression test for #447: the StreamWorker
// sleeps while no stream needs it, instead of spinning a core as soon as
// any stream has been registered.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The engine runs on miniaudio's null backend
// (internal::setNullAudioBackendForTests()), a device-less clock that still
// drives the real mixer callback. The test reads the process CPU time
// (getrusage / GetProcessTimes) over about 1 s while the main thread sleeps,
// and listens on AudioEngine::audioOut for the level of the mix: the test
// files hold DC levels, so the level tells which part of a file is playing
// and a block that holds less is a gap.
//
// Guards the invariants:
// - With a stream that has played to its end (its voice still held by the
//   Sound), the process uses well under one core (the worker used to spin:
//   ~1 core).
// - With one stream playing, the process uses well under one core too, and
//   every audioOut block holds the full level (refills are on time: no gap),
//   also at speed 10, where the ring holds ~34 ms.
// - A seek request on a playing stream is heard within a bound well under
//   the ring's length (the worker wakes for it; the mean is printed for
//   reference, a missed wakeup adds up to the 5 ms poll on average).
//
// The CPU bound is a quarter of one core, far above what the idle engine
// uses (printed as the baseline, before any stream) and far below the spin,
// so a loaded CI runner does not flip it: CPU time counts only the cycles
// this process got.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/resource.h>
    #include <sys/time.h>
#endif

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

// --- Helpers -----------------------------------------------------------------
constexpr int kRate = 48000;   // engine rate = file rate: no resampling

// Bound on the process CPU use while the main thread sleeps, in cores.
constexpr double kMaxCores = 0.25;

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

static bool approx(float a, float b, float tol) { return fabs(a - b) <= tol; }

// User + system CPU time of the whole process, in seconds.
static double processCpuSeconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    auto toSec = [](const FILETIME& f) {
        ULARGE_INTEGER u;
        u.LowPart = f.dwLowDateTime;
        u.HighPart = f.dwHighDateTime;
        return (double)u.QuadPart * 1e-7;   // 100 ns units
    };
    return toSec(kernel) + toSec(user);
#else
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    auto toSec = [](const timeval& t) { return (double)t.tv_sec + (double)t.tv_usec * 1e-6; };
    return toSec(ru.ru_utime) + toSec(ru.ru_stime);
#endif
}

// The process CPU use, in cores, over `ms` of wall time spent sleeping.
static double measureCores(int ms) {
    const auto w0 = chrono::steady_clock::now();
    const double c0 = processCpuSeconds();
    sleepMs(ms);
    const double c1 = processCpuSeconds();
    const double wall = chrono::duration<double>(chrono::steady_clock::now() - w0).count();
    return wall > 0.0 ? (c1 - c0) / wall : 0.0;
}

static string fmt(double v, const char* unit) {
    char b[64];
    snprintf(b, sizeof(b), "%.3f %s", v, unit);
    return b;
}

// 16-bit stereo PCM WAV made of DC segments {seconds, level}.
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

// While g_watch is set, the lowest block level and the number of blocks.
static atomic<bool> g_watch{false};
static atomic<float> g_minLevel{1.0f};
static atomic<int> g_watchedBlocks{0};

// A seek in flight: the first block at g_switchLevel stores its time.
static atomic<float> g_switchLevel{-1.0f};
static atomic<int64_t> g_switchNs{0};

static int64_t nowNs() {
    return chrono::duration_cast<chrono::nanoseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    // A StreamWorker stuck in one stream never returns, and its static
    // destructor would then hang the exit: fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    // Device-less engine; set before anything opens a context.
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();   // this thread is the main thread

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
        const float level = b.frameCount > 0 ? (float)(sum / b.frameCount) : 0.0f;
        g_level.store(level);
        if (g_watch.load()) {
            if (level < g_minLevel.load()) g_minLevel.store(level);
            g_watchedBlocks.fetch_add(1);
        }
        const float target = g_switchLevel.load();
        if (target >= 0.0f && g_switchNs.load() == 0 && approx(level, target, 0.02f)) {
            g_switchNs.store(nowNs());
        }
    });

    const fs::path dir = fs::temp_directory_path()
        / ("tc_streamWorkerIdle_" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path shortWav = dir / "short.wav";   // 0.1 for 0.2 s
    const fs::path bgmWav = dir / "bgm.wav";       // 0.5 for 3 s
    const fs::path dcWav = dir / "dc.wav";         // 0.1 for 0-1 s, 0.5 for 1-3 s
    check("test files are written",
          writeWav(shortWav, {{0.2f, 0.1f}}) && writeWav(bgmWav, {{3.0f, 0.5f}}) &&
          writeWav(dcWav, {{1.0f, 0.1f}, {2.0f, 0.5f}}));

    // The engine on its own, before any stream (for reference).
    sleepMs(100);
    const double baseline = measureCores(1000);
    printf("  baseline (engine running, no stream yet): %s\n", fmt(baseline, "cores").c_str());

    // --- idle after a stream has ended ---------------------------------------------
    {
        Sound s;   // holds the ended voice, and with it the stream, for the measure
        check("loadStream() opens the short file", (bool)s.loadStream(shortWav) && s.isStreaming());
        check("the short stream plays", s.play());
        bool ended = false;
        for (int i = 0; i < 400 && !ended; ++i) {
            sleepMs(5);
            ended = !s.isPlaying();
        }
        check("the short stream plays to its end", ended);
        sleepMs(100);
        const double idle = measureCores(1000);
        printf("  idle after the stream ended: %s\n", fmt(idle, "cores").c_str());
        check("idle after a stream ended: the process uses well under one core",
              idle < kMaxCores, fmt(idle, "cores"));
    }

    // --- one stream playing --------------------------------------------------------
    {
        Sound s;
        check("loadStream() opens the BGM file", (bool)s.loadStream(bgmWav));
        s.setLoop(true);
        check("the BGM stream plays", s.play());
        sleepMs(150);
        g_minLevel.store(1.0f);
        g_watchedBlocks.store(0);
        g_watch.store(true);
        const double playing = measureCores(1000);
        g_watch.store(false);
        printf("  one stream playing: %s\n", fmt(playing, "cores").c_str());
        check("one stream playing: the process uses well under one core",
              playing < kMaxCores, fmt(playing, "cores"));
        const int blocks = g_watchedBlocks.load();
        const float minLevel = g_minLevel.load();
        check("one stream playing: every block holds the full level (no gap)",
              blocks > 100 && approx(minLevel, 0.5f, 0.02f),
              to_string(blocks) + " blocks, lowest level " + to_string(minLevel));

        // At speed 10 the ring holds ~34 ms: the worker keeps up with it
        // between its polls (and the mixer wakes it when a ring drops below
        // half, for a system timer that stretches the 5 ms poll).
        s.setSpeed(10.0f);
        sleepMs(100);
        g_minLevel.store(1.0f);
        g_watchedBlocks.store(0);
        g_watch.store(true);
        const double fast = measureCores(1000);
        g_watch.store(false);
        printf("  one stream playing at speed 10: %s\n", fmt(fast, "cores").c_str());
        const int fastBlocks = g_watchedBlocks.load();
        const float fastMin = g_minLevel.load();
        check("speed 10: every block holds the full level (no gap)",
              fastBlocks > 100 && approx(fastMin, 0.5f, 0.02f),
              to_string(fastBlocks) + " blocks, lowest level " + to_string(fastMin));
        s.stop();
    }

    // --- seeks are not held back --------------------------------------------------
    {
        Sound s;
        check("loadStream() opens the DC file", (bool)s.loadStream(dcWav));
        s.setLoop(true);
        check("the DC stream plays", s.play());
        sleepMs(150);
        constexpr int kSeeks = 40;
        double sumMs = 0.0, maxMs = 0.0;
        int heard = 0;
        for (int i = 0; i < kSeeks; ++i) {
            // Alternate between the two levels: 0.2 s (0.1) and 2.0 s (0.5).
            const bool high = (i % 2 == 0);
            g_switchNs.store(0);
            g_switchLevel.store(high ? 0.5f : 0.1f);
            const int64_t t0 = nowNs();
            s.setPosition(high ? 2.0f : 0.2f);
            for (int t = 0; t < 500 && g_switchNs.load() == 0; ++t) {
                this_thread::sleep_for(chrono::microseconds(500));
            }
            const int64_t t1 = g_switchNs.load();
            g_switchLevel.store(-1.0f);
            if (t1 != 0) {
                const double ms = (double)(t1 - t0) * 1e-6;
                sumMs += ms;
                if (ms > maxMs) maxMs = ms;
                ++heard;
            }
            sleepMs(20 + (i * 7) % 11);   // vary the phase against the device clock
        }
        const double meanMs = heard > 0 ? sumMs / heard : 0.0;
        printf("  seek to the new level heard: mean %.2f ms, max %.2f ms (%d seeks)\n",
               meanMs, maxMs, heard);
        check("every seek is heard", heard == kSeeks, to_string(heard) + " of " + to_string(kSeeks));
        // The null device runs in ~10 ms steps; a seek the worker does not
        // wake for still waits at most its 5 ms poll. 100 ms is a fraction of
        // the ring (~340 ms) and leaves room for a slow runner.
        check("each seek is heard within 100 ms", maxMs < 100.0, fmt(maxMs, "ms"));
        s.stop();
    }

    engine.shutdown();
    fs::remove_all(dir, ec);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
