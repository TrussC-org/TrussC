#define TRUSSC_SHOW_CONSOLE
#include <TrussC.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int kRate = 48000;
constexpr int kSeconds = 60;
constexpr int64_t kTimeoutNs = 2000000000;
constexpr float kBase = 0.02f, kRange = 0.08f;

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}
float level(double seconds) { return kBase + kRange * float(seconds / kSeconds); }
float position(float sample) { return (sample - kBase) * (kSeconds / kRange); }

struct TempWave {
    std::filesystem::path dir;
    TempWave() {
        std::random_device random;
        for (int i = 0; i < 100; ++i) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("tc-stream-seek-" + std::to_string(random()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { dir = candidate; return; }
        }
        throw std::runtime_error("Cannot create a unique temporary directory");
    }
    ~TempWave() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    std::filesystem::path write() const {
        auto path = dir / "position.wav";
        std::ofstream out(path, std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        auto le = [&](uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) out.put(char((value >> (8 * i)) & 255));
        };
        // IEEE float WAV, mono, 48 kHz. Nonzero ramp distinguishes silence.
        constexpr uint32_t frames = kRate * kSeconds, bytes = frames * 4;
        out.write("RIFF", 4); le(48 + bytes, 4); out.write("WAVEfmt ", 8);
        le(16, 4); le(3, 2); le(1, 2); le(kRate, 4); le(kRate * 4, 4);
        le(4, 2); le(32, 2);
        out.write("fact", 4); le(4, 4); le(frames, 4);
        out.write("data", 4); le(bytes, 4);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            float sample = level(double(frame) / kRate);
            uint32_t bits;
            static_assert(sizeof(sample) == sizeof(bits));
            std::memcpy(&bits, &sample, sizeof(bits));
            le(bits, 4);
        }
        return path;
    }
};

struct Trial {
    float target = 0, from = 0;
    int64_t t0 = 0;
    float matchedPosition = 0;
    int callbackFrames = 0, sampleOffset = 0;
    std::atomic<int64_t> t1{0}; // Release publishes the callback's result fields.
};

// All captured state outlives this guard. Stop/join the device before destroying it.
struct AudioSession {
    tc::AudioEngine& engine;
    tc::EventListener listener;
    explicit AudioSession(tc::AudioEngine& e) : engine(e) {}
    ~AudioSession() { listener.disconnect(); engine.shutdown(); }
};

int run(int argc, char** argv) {
    int count = 300;
    uint32_t seed = 550;
    bool nullBackend = false;
    std::string csvPath;
    auto integer = [](const std::string& s, uint64_t max) {
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Expected a nonnegative integer: " + s);
        auto n = std::stoull(s);
        if (n > max) throw std::runtime_error("Integer out of range: " + s);
        return n;
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "streamSeekLatency [--count N] [--csv path] [--seed N] [--null]\n"
                         "Default: 300 seeks on the real default output device.\n"
                         "--null is a logic smoke test; its timings mean nothing.\n";
            return 0;
        } else if (arg == "--null") nullBackend = true;
        else if ((arg == "--count" || arg == "--csv" || arg == "--seed") && i + 1 < argc) {
            std::string value = argv[++i];
            if (arg == "--csv") csvPath = value;
            else if (arg == "--seed") seed = uint32_t(integer(value, UINT32_MAX));
            else {
                count = int(integer(value, 1000000));
                if (count == 0) throw std::runtime_error("--count must be positive");
            }
        } else throw std::runtime_error("Unknown option or missing value: " + arg);
    }
    std::ofstream csv;
    if (!csvPath.empty()) {
        csv.open(csvPath);
        if (!csv) throw std::runtime_error("Cannot open CSV: " + csvPath);
        csv.exceptions(std::ios::badbit | std::ios::failbit);
    }
    TempWave wave;
    auto path = wave.write();
    auto trials = std::make_unique<Trial[]>(count);
    std::atomic<int> active{-1}, observedFrame{-1}, minFrames{INT_MAX}, maxFrames{0};
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<int64_t>::is_always_lock_free);
    tc::getMainThreadId();
    if (nullBackend) tc::internal::setNullAudioBackendForTests(true);
    auto& engine = tc::AudioEngine::getInstance();
    AudioSession session(engine);
    tc::AudioSettings settings;
    settings.sampleRate = kRate;
    settings.channels = 2;
    if (!engine.init(settings))
        throw std::runtime_error("No usable default audio output device. Connect a device, or use --null for a smoke test.");

    // Both revisions expose this diagnostics helper, but no public backend getter.
    // It is used ONLY for metadata and rejecting silent fallback, never detection.
    const auto device = tc::internal::audioDeviceReport(false);
    auto backend = device.backend;
    std::transform(backend.begin(), backend.end(), backend.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (!nullBackend && (backend.empty() || backend == "null" || device.outputDevice.empty()))
        throw std::runtime_error("No real default audio output device (backend: " + device.backend +
                                 "). Use --null only for a logic smoke test.");
    std::cout << "backend: " << device.backend << "\ndevice: " << device.outputDevice
              << "\nsample_rate_hz: " << engine.getSampleRate()
              << "\ndevice_sample_rate_hz: " << device.deviceSampleRate
              << "\ndevice_period_frames: " << device.periodFrames
              << "\nengine_buffer_frames: " << engine.getBufferSize()
              << "\nseed: " << seed << '\n';
    if (nullBackend) std::cout << "NULL BACKEND: logic smoke test only; these latency numbers mean nothing.\n";
    std::cout.flush();

    session.listener = engine.audioOut.listen([&](tc::AudioOutBuffer& b) {
        const int64_t entry = nowNs(); // Timestamp before scanning; no main-thread polling bias.
        if (b.frameCount <= 0 || b.channels <= 0) return;
        minFrames.store(std::min(minFrames.load(std::memory_order_relaxed), b.frameCount), std::memory_order_relaxed);
        maxFrames.store(std::max(maxFrames.load(std::memory_order_relaxed), b.frameCount), std::memory_order_relaxed);
        const int index = active.load(std::memory_order_acquire);
        Trial* trial = index >= 0 ? &trials[index] : nullptr;
        if (trial && trial->t1.load(std::memory_order_relaxed) != 0) trial = nullptr;
        const float low = trial ? level(trial->target - 0.001) : 0;
        const float high = trial ? level(trial->target + 2.1) : 0;
        int lastFrame = -1;
        for (int f = 0; f < b.frameCount; ++f) {
            const float sample = b.data[f * b.channels];
            if (sample >= kBase && sample <= kBase + kRange)
                lastFrame = int(std::lround(position(sample) * kRate));
            if (trial && sample >= low && sample <= high) {
                trial->matchedPosition = position(sample);
                trial->callbackFrames = b.frameCount;
                trial->sampleOffset = f;
                trial->t1.store(entry, std::memory_order_release);
                trial = nullptr; // Preserve the FIRST matching callback.
            }
        }
        if (lastFrame >= 0) observedFrame.store(lastFrame, std::memory_order_relaxed);
    }, tc::audio::priority::Monitor);

    tc::Sound sound;
    if (!sound.loadStream(path) || !sound.play())
        throw std::runtime_error("Could not load/play the generated WAV as a SoundStream");
    const auto warmupDeadline = Clock::now() + std::chrono::seconds(2);
    while (observedFrame.load() < 0 && Clock::now() < warmupDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (observedFrame.load() < 0)
        throw std::runtime_error("No encoded samples in output callbacks within 2 s; cannot measure seeks.");

    std::mt19937 random(seed);
    std::uniform_real_distribution<float> targetSeconds(3.0f, 55.0f);
    float previousTarget = 0;
    for (int i = 0; i < count; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto& trial = trials[i];
        trial.from = float(observedFrame.load()) / kRate;
        do { trial.target = targetSeconds(random); }
        while (std::abs(trial.target - trial.from) < 10 ||
               std::abs(trial.target - previousTarget) < 10);
        previousTarget = trial.target; // Also avoid an outstanding timed-out target.
        active.store(i, std::memory_order_release);
        trial.t0 = nowNs();
        sound.setPosition(trial.target);
        while (trial.t1.load(std::memory_order_acquire) == 0 && nowNs() - trial.t0 < kTimeoutNs)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        active.store(-1, std::memory_order_release);
    }
    session.listener.disconnect();
    engine.shutdown(); // Joins callbacks before reading results or releasing captures.
    sound.stop();

    std::vector<double> latencies;
    if (csv.is_open())
        csv << "seek,from_seconds,target_seconds,status,latency_ms,t0_steady_ns,t1_steady_ns,matched_seconds,callback_frames,sample_offset\n";
    for (int i = 0; i < count; ++i) {
        const auto& trial = trials[i];
        const auto t1 = trial.t1.load(std::memory_order_acquire);
        const bool seen = t1 >= trial.t0 && t1 != 0 && t1 - trial.t0 <= kTimeoutNs;
        const double ms = double(t1 - trial.t0) / 1e6;
        if (seen) latencies.push_back(ms);
        if (csv.is_open()) {
            csv << std::setprecision(10) << i + 1 << ',' << trial.from << ',' << trial.target
                << ',' << (seen ? "observed" : "timeout") << ',';
            if (seen) csv << ms;
            csv << ',' << trial.t0 << ',';
            if (t1 != 0) csv << t1;
            csv << ',';
            if (t1 != 0) csv << trial.matchedPosition;
            csv << ',' << trial.callbackFrames << ',' << trial.sampleOffset << '\n';
        }
    }
    if (csv.is_open()) csv.close();
    std::sort(latencies.begin(), latencies.end());
    std::cout << "callback_frames_min: " << minFrames.load()
              << "\ncallback_frames_max: " << maxFrames.load()
              << "\ncount: " << count << "\nobserved: " << latencies.size()
              << "\ntimeouts_2s: " << count - latencies.size() << '\n';
    if (latencies.empty()) std::cout << "mean_ms: n/a\nmedian_ms: n/a\np95_ms: n/a\nmax_ms: n/a\n";
    else {
        double sum = 0;
        for (double ms : latencies) sum += ms;
        size_t n = latencies.size();
        double median = (latencies[(n - 1) / 2] + latencies[n / 2]) / 2;
        std::cout << std::fixed << std::setprecision(3)
                  << "mean_ms: " << sum / n << "\nmedian_ms: " << median
                  << "\np95_ms: " << latencies[size_t(std::ceil(0.95 * n)) - 1]
                  << "\nmax_ms: " << latencies.back() << '\n';
    }
    return 0; // Timeouts and latency values are measurements, never assertions (#527).
}
} // namespace

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) {
        std::cerr << "streamSeekLatency: " << e.what() << '\n';
        return 1;
    }
}
