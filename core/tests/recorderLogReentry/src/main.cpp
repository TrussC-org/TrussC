// Reentrant onLog callbacks run through the recorder's real locking paths.
// Dummy encoder hooks make failures deterministic, including the common Metal
// completion path, without requiring a GPU or a particular encoder installation.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace tc;
#ifdef _WIN32
bool testWindowsStartupLogReentry();
#endif
namespace {
enum class Fault { None, Close, Audio, Submit, LockCaller, LockBackend, SourceLayout, DestinationLayout, LockLayout, CopyLayout };
Fault fault = Fault::None;
const char* lockMessage = "";
int closes = 0, audioWrites = 0, submits = 0, readbacks = 0;
unsigned char frame[16 * 16 * 4]{};
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!ok) ++failures;
}
struct Readback {
    internal::RecorderFrameLayout getLayout() const {
        return internal::recorderPackedLayout(fault == Fault::SourceLayout ? 32 : 16, 16,
                                              internal::RecorderPixelFormat::BGRA8);
    }
    bool readInto(unsigned char*, size_t, size_t, bool) const {
        ++readbacks;
        return fault != Fault::CopyLayout;
    }
};
}

// These replace only the encoder. Logger, State, audio drain, stop, completion
// handshake, diagnostic batching and mutexes are the production implementations.
namespace trussc {
bool VideoWriter::openPlatform(const std::string&, int, int, float, const VideoRecordSettings&) { return true; }
bool VideoWriter::appendPlatform(const unsigned char*, double) { return fault != Fault::Submit; }
#if TC_ASYNC_SCREEN_CAPTURE
bool VideoWriter::appendPlatformDeferred(const unsigned char* rgba, double timeSec, Diagnostics&) {
    return appendPlatform(rgba, timeSec);
}
#endif
bool VideoWriter::appendAudioPlatform(const float*, int, double) {
    ++audioWrites;
    return fault != Fault::Audio;
}
SaveResult VideoWriter::closePlatform() {
    ++closes;
    return fault == Fault::Close
        ? SaveResult::fail(SaveError::WriteFailed, "dummy finalization failed")
        : SaveResult::success();
}
unsigned char* VideoWriter::lockFramePlatform(internal::RecorderFrameBuffer& buffer, Diagnostics& diagnostics) {
    if (fault == Fault::LockCaller) {
        callFailed(lockMessage, &diagnostics);
        return nullptr;
    }
    if (fault == Fault::LockBackend) {
        result_ = SaveResult::fail(SaveError::WriteFailed, "dummy encoder is not writing");
        backendRejected("AVAssetWriter is not writing", &diagnostics);
        return nullptr;
    }
    if (fault == Fault::LockLayout) {
        rejectFrameLayout(&diagnostics);
        return nullptr;
    }
    buffer = {frame, internal::recorderPackedLayout(16, fault == Fault::DestinationLayout ? 8 : 16,
                                                  internal::RecorderPixelFormat::BGRA8)};
    return frame;
}
void VideoWriter::discardFramePlatform() {}
bool VideoWriter::submitFramePlatform(double) { ++submits; return fault != Fault::Submit; }
}
namespace trussc::internal {
struct ScreenRecorderTestAccess {
    static void audio(ScreenRecorder& recorder) {
        auto& s = *recorder.state_;
        s.audioRate_ = 48000;
        s.audioCh_ = 2;
        s.audioRing_.assign(16, 0.25f);
        s.audioHead_ = 4;
        s.audioTail_ = 0;
        s.anchored_ = true;
        s.audioActive_ = true;
        s.source_ = ScreenRecorder::State::Source::Swapchain;
        s.pacer_.start(60.0, 0.0);
    }
    static void afterFrame(ScreenRecorder& recorder) { recorder.state_->onAfterFrame(); }
    static void complete(ScreenRecorder& recorder, bool close, bool failedStart) {
        auto state = recorder.state_;
        state->inFlight_ = 1;
        state->closeWhenDone_ = close;
        if (failedStart) state->finishCaptureAndLog(fault == Fault::SourceLayout);
        else state->completeCapture(Readback{}, 0.0);
    }
    static bool complete(const ScreenRecorder& recorder) { return recorder.state_->inFlight_ == 0; }
    static void pending(ScreenRecorder& recorder) { recorder.state_->inFlight_ = 1; }
};
}

TC_CORE_TEST_MAIN() {
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        std::fprintf(stderr, "FAIL: recorder onLog reentry timed out\n");
        std::_Exit(3);
    }).detach();
    getMainThreadId();
    getLogger().setLogLevel(LogLevel::Verbose);
    const auto path = fs::temp_directory_path() / "tc-recorder-log-reentry.mp4";
    using Access = internal::ScreenRecorderTestAccess;
    enum class Site { Stop, Drain, Complete, FailedStart };
    const auto run = [&](const char* name, Site site, Fault failure, bool audio,
                         bool writerAudio, bool mistake, const std::vector<std::string>& expected) {
        fault = failure;
        closes = audioWrites = submits = readbacks = 0;
        ScreenRecorder recorder;
        VideoRecordSettings settings;
        settings.audio = writerAudio;
        check(recorder.writer().open(path, 16, 16, settings).ok(), "dummy writer opens");
        if (mistake) recorder.writer().addFrameAt(nullptr, 16, 16, 0.0);
        if (audio) Access::audio(recorder);
        std::vector<std::string> messages;
        bool completeBeforeLog = true;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find("[VideoWriter]") == std::string::npos) return;
            messages.push_back(e.message);
            if (site == Site::Complete || site == Site::FailedStart)
                completeBeforeLog &= Access::complete(recorder);
            recorder.stop(); // Deliberately reenter on EVERY diagnostic, including nested close logs.
        });
        switch (site) {
            case Site::Stop: recorder.stop(); break;
            case Site::Drain: Access::afterFrame(recorder); break;
            case Site::Complete:
            case Site::FailedStart: {
                std::thread completion([&] { Access::complete(recorder, true, site == Site::FailedStart); });
                completion.join();
                break;
            }
        }
        bool content = messages.size() == expected.size();
        for (size_t i = 0; i < messages.size() && i < expected.size(); ++i)
            content &= messages[i].find(expected[i]) != std::string::npos;
        check(content && completeBeforeLog && closes == 1 && !recorder.writer().isOpen(), name);
        check(!audio || audioWrites == (writerAudio ? 1 : 0), "audio drain retains its submission count");
    };
    run("stop saved Notice reenters stop", Site::Stop, Fault::None, false, false, false, {"saved 0 frames -> "});
    run("stop finalization Error reenters stop", Site::Stop, Fault::Close, false, false, false, {"dummy finalization failed"});
    run("stop zero-frame Warning reenters stop", Site::Stop, Fault::None, false, false, true, {"take accepted 0 frames after caller mistakes -> "});
    run("stop Error and Warning retain their order", Site::Stop, Fault::Close, false, false, true,
        {"dummy finalization failed", "take accepted 0 frames after caller mistakes -> "});
    for (auto site : {Site::Stop, Site::Drain, Site::Complete, Site::FailedStart}) {
        run("audio backend rejection reenters stop", site, Fault::Audio, true, true, false, {"encoder rejected audio"});
        // A successful synthetic frame precedes the late audio drain.
        const int frames = site == Site::Complete ? 1 : 0;
        run("audio caller error reenters stop", site, Fault::None, true, false, false,
            frames ? std::vector<std::string>{"writer has no audio track", "saved 1 frames -> "}
                   : std::vector<std::string>{"writer has no audio track", "take accepted 0 frames after caller mistakes -> "});
    }
    for (auto site : {Site::Complete, Site::FailedStart}) {
        run("late completion saved Notice reenters stop", site, Fault::None, false, false, false,
            {site == Site::Complete ? "saved 1 frames -> " : "saved 0 frames -> "});
        run("late completion finalization Error reenters stop", site, Fault::Close, false, false, false, {"dummy finalization failed"});
        run("late completion zero-frame Warning reenters stop", site, Fault::None, false, false, true,
            {site == Site::Complete ? "saved 1 frames -> " : "take accepted 0 frames after caller mistakes -> "});
    }
    for (const char* message : {"encoder frame buffer unavailable or already locked", "input not ready (timeout)",
                               "could not create pixel buffer"}) {
        lockMessage = message;
        run("completion lockFrame caller Error reenters stop", Site::Complete, Fault::LockCaller, false, false, false,
            {message, "take accepted 0 frames after caller mistakes -> "});
    }
    run("completion lockFrame backend Error reenters stop", Site::Complete, Fault::LockBackend, false, false, false,
        {"dummy encoder is not writing"});
    run("completion submitFrame Error reenters stop", Site::Complete, Fault::Submit, false, false, false,
        {"encoder rejected frame 0"});
    for (auto failure : {Fault::SourceLayout, Fault::DestinationLayout, Fault::LockLayout, Fault::CopyLayout}) {
        run("completion layout Warning reenters stop after completion publication", Site::Complete,
            failure, false, false, false,
            {"frame layout does not match the recording buffer; frame skipped", "saved 0 frames -> "});
        check(submits == 0 && readbacks == (failure == Fault::CopyLayout ? 1 : 0),
              "layout rejection prevents unsafe copy and submission");
    }
    run("failed-start layout Warning reenters stop after completion publication", Site::FailedStart,
        Fault::SourceLayout, false, false, false,
        {"frame layout does not match the recording buffer; frame skipped", "saved 0 frames -> "});
    // Hold one stop's inline diagnostic across another stop. On success,
    // destroy the owner before joining the first stop so joining cannot mask
    // a listener that still uses the public recorder.
    for (auto failure : {Fault::None, Fault::LockCaller, Fault::Close}) {
        fault = failure;
        closes = 0;
        auto recorder = std::make_unique<ScreenRecorder>();
        auto* raw = recorder.get();
        raw->writer().open(path, 16, 16);
        if (failure == Fault::LockCaller) raw->writer().addFrameAt(nullptr, 16, 16, 0.0);
        const char* match = failure == Fault::None ? "saved 0 frames -> "
            : failure == Fault::Close ? "dummy finalization failed"
            : "take accepted 0 frames after caller mistakes -> ";
        std::atomic<bool> entered{false}, resume{false}, stopping{false};
        std::atomic<bool> listenerReturned{false}, reentrantSucceeded{false};
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find(match) == std::string::npos) return;
            entered.store(true);
            while (!resume.load()) std::this_thread::yield();
            reentrantSucceeded.store(raw->stop().ok());
            listenerReturned.store(true);
        });
        std::thread firstStop([&] { raw->stop(); });
        while (!entered.load()) std::this_thread::yield();
        std::thread releaseListener([&] {
            while (!stopping.load()) std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            resume.store(true);
        });
        stopping.store(true);
        auto stopped = raw->stop();
        const bool drained = listenerReturned.load();
        listener.disconnect();
        // A failed stop provides no lifetime guarantee. Drain before destruction
        // on failure, while retaining the successful-stop destruction ordering.
        if (!stopped) {
            resume.store(true);
            firstStop.join();
        }
        recorder.reset();
        releaseListener.join();
        if (firstStop.joinable()) firstStop.join();
        check(drained && stopped.ok() == (failure != Fault::Close)
              && reentrantSucceeded.load() == (failure != Fault::Close) && closes == 1,
              failure == Fault::None ? "overlapping stop drains saved Notice delivery"
              : failure == Fault::Close ? "overlapping stop drains finalization Error delivery"
              : "overlapping stop drains zero-frame Warning delivery");
    }
    {
        fault = Fault::None;
        ScreenRecorder recorder;
        recorder.writer().open(path, 16, 16);
        std::atomic<bool> entered{false}, resume{false};
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find("saved 0 frames -> ") == std::string::npos) return;
            entered.store(true);
            while (!resume.load()) std::this_thread::yield();
            recorder.stop();
        });
        std::thread firstStop([&] { recorder.stop(); });
        while (!entered.load()) std::this_thread::yield();
        const auto before = std::chrono::steady_clock::now();
        auto stopped = recorder.stop();
        const auto elapsed = std::chrono::steady_clock::now() - before;
        listener.disconnect();
        resume.store(true);
        firstStop.join();
        check(!stopped && elapsed < std::chrono::seconds(4),
              "held stop diagnostic reports failure within the drain bound");
    }
    // Hold an inline completion listener just before its reentrant stop. A
    // successful external stop must drain that listener before its owner can
    // disconnect the listener and destroy the recorder (disconnect alone does
    // not drain a callback already running). Join only AFTER destruction so
    // that joining cannot hide the lifetime regression from ASan/TSan.
    for (bool layoutWarning : {false, true}) {
      for (auto site : {Site::Complete, Site::FailedStart, Site::Drain}) {
        if (layoutWarning && site == Site::Drain) continue;
        const bool failedStart = site == Site::FailedStart;
        fault = layoutWarning ? Fault::SourceLayout
            : site == Site::Drain ? Fault::None : Fault::LockCaller;
        lockMessage = "input not ready (timeout)";
        auto recorder = std::make_unique<ScreenRecorder>();
        auto* raw = recorder.get();
        raw->writer().open(path, 16, 16);
        if (site == Site::Drain) Access::audio(*raw);
        std::atomic<bool> entered{false}, resume{false}, stopping{false};
        std::atomic<bool> listenerReturned{false}, reentrantSucceeded{false};
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            const char* match = layoutWarning ? "frame layout does not match the recording buffer; frame skipped"
                : failedStart ? "saved 0 frames -> "
                : site == Site::Drain ? "writer has no audio track" : "input not ready (timeout)";
            if (e.message.find(match) == std::string::npos) return;
            entered.store(true);
            while (!resume.load()) std::this_thread::yield();
            reentrantSucceeded.store(raw->stop().ok());
            listenerReturned.store(true);
        });
        std::thread completion([&] {
            if (site == Site::Drain) Access::afterFrame(*raw);
            else Access::complete(*raw, failedStart, failedStart);
        });
        while (!entered.load()) std::this_thread::yield();
        std::thread releaseListener([&] {
            while (!stopping.load()) std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            resume.store(true);
        });
        stopping.store(true);
        auto stopped = raw->stop();
        const bool drained = listenerReturned.load();
        listener.disconnect();
        recorder.reset();
        releaseListener.join();
        completion.join();
        check(stopped.ok() && drained && reentrantSucceeded.load(), layoutWarning
              ? "successful teardown drains layout Warning before owner destruction"
              : failedStart ? "successful teardown drains failed-start diagnostic before owner destruction"
              : site == Site::Drain ? "successful teardown drains per-frame diagnostic before owner destruction"
              : "successful teardown drains completion diagnostic before owner destruction");
      }
    }
    {
        fault = Fault::LockCaller;
        lockMessage = "input not ready (timeout)";
        ScreenRecorder recorder;
        recorder.writer().open(path, 16, 16);
        std::atomic<bool> entered{false}, resume{false};
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find(lockMessage) == std::string::npos) return;
            entered.store(true);
            while (!resume.load()) std::this_thread::yield();
        });
        std::thread completion([&] { Access::complete(recorder, false, false); });
        while (!entered.load()) std::this_thread::yield();
        auto stopped = recorder.stop();
        listener.disconnect();
        resume.store(true);
        completion.join();
        check(!stopped && !recorder.writer().isOpen(),
              "diagnostic timeout rejects teardown and completion honors late close request");
    }
    // Without a prior close request, the diagnostic listener initiates close.
    {
        fault = Fault::Submit;
        ScreenRecorder recorder;
        recorder.writer().open(path, 16, 16);
        int logs = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find("encoder rejected frame 0") == std::string::npos) return;
            ++logs;
            check(Access::complete(recorder), "completion count is published before submission Error");
            recorder.stop();
        });
        Access::complete(recorder, false, false);
        check(logs == 1 && !recorder.writer().isOpen(), "submission Error listener initiates stop");
    }
    {
        ScreenRecorder recorder;
        Access::pending(recorder);
        int logs = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find("recording still has frames in flight") == std::string::npos) return;
            ++logs;
            recorder.stop();
        });
        auto result = recorder.stop();
        Access::complete(recorder, true, true);
        check(!result && logs == 1, "pending-capture Error reenters stop once");
    }
    {
        // A bare writer reports immediately. Preserve the rejected call's
        // return value even when its listener starts a successful new take.
        fault = Fault::Submit;
        VideoWriter writer;
        writer.open(path, 16, 16);
        int errors = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.find("encoder rejected frame 0") == std::string::npos) return;
            ++errors;
            writer.open(path, 16, 16);
        });
        auto rejected = writer.addFrameAt(frame, 16, 16, 0.0);
        check(!rejected && errors == 1 && writer.isOpen(),
              "bare writer retains submission failure across onLog restart");
    }
#ifdef _WIN32
    check(testWindowsStartupLogReentry(), "Windows AAC startup Error permits onLog startup reentry once");
#endif
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
