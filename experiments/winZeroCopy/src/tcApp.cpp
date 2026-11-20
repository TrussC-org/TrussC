#include "tcApp.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
using namespace tc;

namespace {
double processSeconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    auto ticks = [](FILETIME value) {
        return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return double(ticks(kernel) + ticks(user)) / 10000000.0;
#else
    return -1;  // Linux smoke checks CSV/arguments; no substitute CPU metric.
#endif
}
std::string privateBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        return std::to_string(counters.PrivateUsage);
#endif
    return {};
}
double logicalCpus() {
#ifdef _WIN32
    // Includes all processor groups, rather than silently capping large machines at 64.
    return double(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
#else
    return 0;
#endif
}
std::string csv(const std::string& value) {
    std::string result = "\"";
    for (char c : value) result += c == '"' ? "\"\"" : std::string(1, c);
    return result + '"';
}
}

bool tcApp::openPlayers() {
    videos_.clear();
    frames_.assign(size_t(options.players), 0);
    for (int i = 0; i < options.players; ++i) {
        auto video = std::make_unique<VideoPlayer>();
        const auto& clip = options.clips[options.clips.size() == 1 ? 0 : size_t(i)];
        if (!video->load(clip)) {
            logError("WinZeroCopy") << "Cannot load " << pathToUtf8(clip);
            phase_ = Phase::Finished;
            exitApp(1);
            return false;
        }
        video->setLoop(true);
        video->setVolume(0);
        video->play();
        videos_.push_back(std::move(video));
    }
    phaseStart_ = Clock::now();
    return true;
}

void tcApp::setup() {
    // tc_get_screenshot needs the ReadOnly grant.
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    phase_ = options.openclose ? Phase::OpenClose : Phase::Warmup;
    openPlayers();
}

void tcApp::update() {
    if (phase_ == Phase::Finished) return;
    for (auto& video : videos_) {
        video->update();
        if (video->hasError()) {
            logError("WinZeroCopy") << "Playback failed; aborting sample";
            phase_ = Phase::Finished;
            exitApp(1);
            return;
        }
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - phaseStart_).count();
    if (phase_ == Phase::OpenClose) {
        if (elapsed < 2) return;
        videos_.clear();
        phase_ = Phase::OpenCloseDrain;
        drainFrames_ = 0;
    } else if (phase_ == Phase::OpenCloseDrain) {
        // Two frame boundaries after close let deferred GPU retirement drain.
        if (++drainFrames_ < 2) return;
        const auto bytes = privateBytes();
        // Cycle zero is the extra warm-up open/close, outside the K measured cycles.
        if (cycles_ == 0) privateWarm_ = bytes;
        if (cycles_ == options.openclose / 2) privateMid_ = bytes;
        if (cycles_ == options.openclose) {
            privateAfter_ = bytes;
            logNotice("WinZeroCopy") << "openclose=" << cycles_ << " private_bytes_warm=" << privateWarm_
                << " private_bytes_mid=" << privateMid_ << " private_bytes_after=" << privateAfter_;
            phase_ = Phase::Warmup;
        } else {
            ++cycles_;
            phase_ = Phase::OpenClose;
        }
        openPlayers();
    } else if (phase_ == Phase::Warmup) {
        for (auto& video : videos_) liveDrawn_ = liveDrawn_ || video->isFrameNew();
    } else if (phase_ == Phase::Screenshot) {
        if (elapsed >= 5) {
            for (auto& video : videos_) video->setPaused(false);
            phase_ = Phase::Measure;
            phaseStart_ = Clock::now();
            cpuStart_ = processSeconds();
            draws_ = 0;
            std::fill(frames_.begin(), frames_.end(), 0);
        }
    } else {
        for (size_t i = 0; i < videos_.size(); ++i)
            if (videos_[i]->isFrameNew()) ++frames_[i];
        if (elapsed >= options.seconds) finish(elapsed);
    }
}

void tcApp::draw() {
    clear(0.08f);
    const int columns = int(std::ceil(std::sqrt(double(options.players))));
    const int rows = (options.players - 1) / columns + 1;
    const float tileWidth = float(getWidth()) / columns;
    const float tileHeight = float(getHeight()) / rows;
    for (size_t i = 0; i < videos_.size(); ++i)
        videos_[i]->draw(float(i % columns) * tileWidth, float(i / columns) * tileHeight, tileWidth, tileHeight);
    if (phase_ == Phase::Measure) ++draws_;
    if (phase_ == Phase::Warmup && liveDrawn_ &&
        std::chrono::duration<double>(Clock::now() - phaseStart_).count() >= options.warmup) {
        // Hold a live frame for MCP capture, outside the measured playback window.
        for (auto& video : videos_) video->setPaused(true);
        phase_ = Phase::Screenshot;
        phaseStart_ = Clock::now();
        logNotice("WinZeroCopy") << "READY_FOR_SCREENSHOT";
    }
}

void tcApp::finish(double elapsed) {
    const double cpuEnd = processSeconds();
    const double cpus = logicalCpus();
    const bool hasCpu = cpuStart_ >= 0 && cpuEnd >= cpuStart_ && cpus > 0;
    const char* experiment = std::getenv("TRUSSC_EXP_WIN_ZEROCOPY");
    const char* mode = experiment && std::strcmp(experiment, "1") == 0 ? "zerocopy" : "baseline";
    std::ofstream out(options.output);
    if (!out) {
        logError("WinZeroCopy") << "Cannot write CSV: " << pathToUtf8(options.output);
        exitApp(1);
        return;
    }
    out << "mode,clip,resolution,player_index,players,seconds,new_frames,new_frame_fps,expected_frames,drops,process_cpu_percent,window_fps,private_bytes_warm,private_bytes_mid,private_bytes_after\n";
    out << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < videos_.size(); ++i) {
        const auto& video = *videos_[i];
        const double rate = video.getFrameRate();
        const auto& clip = options.clips[options.clips.size() == 1 ? 0 : i];
        out << mode << ',' << csv(pathToUtf8(clip)) << ',' << int(video.getWidth()) << 'x' << int(video.getHeight())
            << ',' << i << ',' << options.players << ',' << elapsed << ',' << frames_[i] << ',' << frames_[i] / elapsed << ',';
        if (rate > 0) out << rate * elapsed;
        out << ',';
        if (rate > 0) out << rate * elapsed - double(frames_[i]);
        out << ',';
        if (hasCpu) out << (cpuEnd - cpuStart_) / elapsed / cpus * 100.0;
        out << ',' << draws_ / elapsed << ',' << privateWarm_ << ',' << privateMid_ << ',' << privateAfter_ << '\n';
    }
    out.close();
    if (!out) {
        logError("WinZeroCopy") << "CSV write failed";
        exitApp(1);
        return;
    }
    logNotice("WinZeroCopy") << "mode=" << mode << " seconds=" << elapsed << " window_fps=" << draws_ / elapsed
        << " CSV=" << pathToUtf8(options.output);
    phase_ = Phase::Finished;
    exitApp();
}
