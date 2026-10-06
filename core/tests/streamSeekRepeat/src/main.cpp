// #582: repeat the pending seek on a non-looping decoder already at its end.
// The null backend drives the real worker and mixer without an audio device.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcStreamSeekDiagnostics.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

namespace {
constexpr int kRate = 48000;
constexpr int kRepeats = 50;
atomic<float> g_level{0.0f};
atomic<uint64_t> g_frames{0};
mutex g_blockMutex;
bool g_record = false;   // protected by g_blockMutex, as is g_blockLevels
vector<float> g_blockLevels;

template <class Pred>
bool waitFor(Pred pred) {
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(2);
    do {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    } while (chrono::steady_clock::now() < deadline);
    return pred();
}

bool writeTailWav(const fs::path& path) {
    // Same 0.3 s file as streamSeek's pending case: 0.5, then 0.1.
    ofstream f(path, ios::binary | ios::trunc);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    constexpr uint32_t frames = kRate * 3 / 10;
    f.write("RIFF", 4); u32(36 + frames * 4); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(kRate); u32(kRate * 4);
    u16(4); u16(16);
    f.write("data", 4); u32(frames * 4);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t value = (int16_t)lround((i < kRate / 10 ? 0.5f : 0.1f) * 32768.0f);
        u16((uint16_t)value); u16((uint16_t)value);
    }
    return (bool)f;
}

bool fail(int iteration, const char* reason, const Sound& sound) {
    printf("pending repeat %d/%d FAIL: %s -- %s\n", iteration, kRepeats, reason,
           streamSeekFailureState(sound, g_level.load()).c_str());
    fflush(stdout);
    return false;
}

bool runPendingSeek(int iteration, const fs::path& path) {
    Sound sound;
    if (!sound.loadStream(path)) return fail(iteration, "loadStream()", sound);

    // Wait for the decoder's actual end rather than guessing when the
    // worker has read all the frames. Retry if preemption let the voice end.
    bool ready = false;
    for (int attempt = 0; attempt < 3 && !ready; ++attempt) {
        sound.stop();
        if (!sound.play()) return fail(iteration, "play()", sound);
        const bool atEnd = waitFor([&] {
            return internal::streamSeekStateForTests(sound).decoderAtEnd || !sound.isPlaying();
        });
        ready = atEnd && sound.isPlaying() && sound.getPosition() < 0.2f;
    }
    if (!ready) return fail(iteration, "decoder must be at end while the voice plays", sound);

    internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
    sound.setPosition(0.0f);
    const uint64_t started = g_frames.load();
    // Let callbacks cover more than the old 0.3 s file, as the single check
    // does. The condition counts processed audio, not elapsed sleep time.
    const bool pending = waitFor([&] {
        return g_frames.load() - started >= kRate * 4 / 10 && fabs(g_level.load()) < 0.001f;
    });
    bool ok = true;
    if (!pending || !sound.isPlaying()) {
        ok = fail(iteration, "pending seek must stay playing and silent", sound);
    }
    {
        lock_guard<mutex> lock(g_blockMutex);
        g_blockLevels.clear();
        g_record = true;
        // Arm recording before the worker can produce target audio.
        internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    }
    bool ended = false;
    uint64_t endedAt = 0;
    waitFor([&] {
        if (!ended && !sound.isPlaying()) {
            ended = true;
            endedAt = g_frames.load();
        }
        // The mixer can end the voice before audioOut records its final
        // block. Wait for a callback after observing the end.
        return ended && g_frames.load() > endedAt;
    });
    bool heard = false;
    {
        lock_guard<mutex> lock(g_blockMutex);
        g_record = false;
        for (float level : g_blockLevels) {
            if (fabs(level - 0.5f) <= 0.02f) heard = true;
        }
    }
    if (!heard) ok = fail(iteration, "target audio (level 0.5) never arrived", sound);
    sound.stop();
    if (ok) {
        printf("pending repeat %d/%d PASS\n", iteration, kRepeats);
        fflush(stdout);
    }
    return ok;
}
} // namespace

TC_CORE_TEST_MAIN() {
    thread([] {
        this_thread::sleep_for(chrono::seconds(180));
        printf("FAIL: streamSeekRepeat watchdog timeout\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    internal::setNullAudioBackendForTests(true);
    getMainThreadId();
    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = kRate;
    settings.channels = 2;
    settings.bufferSize = 256;
    settings.maxPolyphony = 8;
    if (!engine.init(settings)) {
        printf("FAIL: the audio engine does not start on the null backend\n");
        return 1;
    }
    EventListener levels = engine.audioOut.listen([](AudioOutBuffer& b) {
        double sum = 0.0;
        for (int i = 0; i < b.frameCount; ++i) sum += b.data[i * b.channels];
        const float level = b.frameCount ? (float)(sum / b.frameCount) : 0.0f;
        g_level.store(level);
        lock_guard<mutex> lock(g_blockMutex);
        if (g_record) g_blockLevels.push_back(level);
        // Publish progress only after recording the block.
        g_frames.fetch_add(b.frameCount);
    });
    const auto dir = fs::temp_directory_path() /
        ("tc_streamSeekRepeat_" + to_string(chrono::steady_clock::now().time_since_epoch().count()));
    error_code ec;
    fs::create_directories(dir, ec);
    const auto path = dir / "tail.wav";
    bool ok = !ec && writeTailWav(path);
    if (!ok) printf("FAIL: cannot write the pending-seek fixture\n");
    for (int i = 1; i <= kRepeats && ok; ++i) ok = runPendingSeek(i, path);
    internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    engine.shutdown();
    fs::remove_all(dir, ec);
    return ok ? 0 : 1;
}
