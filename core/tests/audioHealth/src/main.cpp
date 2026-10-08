// #302: stream underruns, stalled callbacks and re-init stops on the null backend.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    printf("%-68s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++failures;
}

template<class Pred> bool waitFor(Pred pred) {
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while (!pred()) {
        if (chrono::steady_clock::now() >= deadline) return false;
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return true;
}

// Park an actual callback after it mixed voices, outside the engine lock.
// Tests wait for conditions, never assert how quickly a condition happened.
struct CallbackGate {
    mutex m;
    condition_variable cv;
    bool requested = false, entered = false, released = false;
    void visit() {
        unique_lock<mutex> lock(m);
        if (!requested) return;
        requested = false;
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
        entered = false;
        cv.notify_all();
    }
    bool park() {
        unique_lock<mutex> lock(m);
        released = false;
        requested = true;
        return cv.wait_for(lock, chrono::seconds(5), [&] { return entered; });
    }
    void resume() {
        unique_lock<mutex> lock(m);
        released = true;
        requested = false;
        cv.notify_all();
        cv.wait(lock, [&] { return !entered; });
    }
};

bool writeWav(const fs::path& p) {
    // Ten seconds of stereo DC, so a silent output block is unambiguous.
    ofstream f(p, ios::binary);
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(char(v >> (8*i))); };
    auto u16 = [&](uint16_t v) { f.put(char(v)); f.put(char(v >> 8)); };
    constexpr uint32_t frames = 480000, bytes = frames * 4;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(2); u32(48000); u32(192000); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < frames; ++i) { u16(8192); u16(8192); }
    return bool(f);
}

Json audioState() {
    const string reply = mcp::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"tc_get_audio_state","arguments":{"devices":false}}})");
    try {
        const auto j = Json::parse(reply);
        return Json::parse(j.at("result").at("content").at(0).at("text").get<string>());
    } catch (...) { return Json(); }
}
}

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    auto& engine = AudioEngine::getInstance();
    mcp::registerInspectionTools();
    check("unused engine has no stall or diagnostic counts",
          !engine.getStats().stalled && engine.getStats().underrunFrames == 0
          && engine.getStats().voicesStoppedByReinit == 0);
    check("MCP polling does not start the engine", audioState().value("running", true) == false);

    mutex logMutex;
    vector<string> warnings;
    bool logsOnMain = true;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& a) {
        if (a.level != LogLevel::Warning) return;
        lock_guard<mutex> lock(logMutex);
        warnings.push_back(a.message);
        logsOnMain = logsOnMain && this_thread::get_id() == getMainThreadId();
    });
    auto countWarnings = [&](const string& text) {
        lock_guard<mutex> lock(logMutex);
        size_t n = 0;
        for (const auto& w : warnings) if (w.find(text) != string::npos) ++n;
        return n;
    };

    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    settings.sampleRate = 48000;
    settings.bufferSize = 256;
    check("null backend starts", engine.init(settings));
    if (!engine.isInitialized()) return 1;
    const auto path = fs::temp_directory_path() / ("tc_audio_health_"
        + to_string(chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    check("fixture written", writeWav(path));

    CallbackGate gate;
    atomic<bool> exactSilentBlock{false};
    atomic<bool> lastBlockSilent{false};
    uint64_t previousUnderruns = 0; // callback-owned
    auto out = engine.audioOut.listen([&](AudioOutBuffer& b) {
        const auto n = engine.getStats().underrunFrames;
        bool silent = true;
        for (int i = 0; i < b.frameCount * b.channels; ++i) silent = silent && b.data[i] == 0;
        lastBlockSilent = silent;
        if (silent && n - previousUnderruns == uint64_t(b.frameCount)) exactSilentBlock = true;
        previousUnderruns = n;
        gate.visit();
    });

    internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
    Sound stream;
    check("stream starts with the decoder held back", bool(stream.loadStream(path)) && stream.play());
    check("startup callback parked", gate.park());
    check("startup silence is excluded", engine.getStats().underrunFrames == 0);
    internal::pumpAudioDiagnostics();
    check("startup wait has no underrun warning", countWarnings("stream underrun") == 0);
    internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    gate.resume();
    check("decoded frames reach the mixer", waitFor([&] { return stream.getPosition() > 0; }));

    internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
    stream.setSpeed(10);
    check("decoder starvation is counted per output frame, not channel",
          waitFor([&] { return engine.getStats().underrunFrames > 0 && exactSilentBlock.load(); }));
    stream.pause();
    check("paused callback parked", gate.park());
    const auto underruns = engine.getStats().underrunFrames;
    check("audio thread has not logged underruns", countWarnings("stream underrun") == 0);
    internal::pumpAudioDiagnostics();
    check("main-thread pump reports underruns", countWarnings("stream underrun") == 1);
    gate.resume();
    check("next paused callback parked", gate.park());
    check("paused silence is excluded", engine.getStats().underrunFrames == underruns);
    stream.setPosition(1);
    stream.resume();
    gate.resume();
    check("pending seek callback parked", gate.park());
    check("pending seek silence is excluded", engine.getStats().underrunFrames == underruns);
    // Unlike getPosition(), the inspection snapshot reads the mixer's
    // positionF, so reaching the target proves the seek has been applied.
    internal::setStreamFaultForTests(internal::StreamFaultForTests::SeekRefillStalls);
    gate.resume();
    check("mixer applies the seek before any refill", waitFor([&] {
        const auto playing = engine.getPlayingSounds();
        return playing.size() == 1 && playing[0].position == 1.0f;
    }));
    check("applied seek callback parked", gate.park());
    check("applied seek refill silence is excluded",
          lastBlockSilent && engine.getStats().underrunFrames == underruns);
    gate.resume();
    check("next unfilled seek callback parked", gate.park());
    check("continued seek refill silence is excluded",
          lastBlockSilent && engine.getStats().underrunFrames == underruns);
    stream.setSpeed(1);
    internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    gate.resume();
    check("playback resumes after seek refill", waitFor([&] { return stream.getPosition() > 1; }));
    internal::setStreamFaultForTests(internal::StreamFaultForTests::Stalls);
    stream.setSpeed(10);
    check("starvation after seek playback is counted again",
          waitFor([&] { return engine.getStats().underrunFrames > underruns; }));
    stream.stop();
    internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    const auto streamUnderruns = engine.getStats().underrunFrames;

    // A sustained nonzero voice lets stale meter values be distinguished from zero.
    Sound tone;
    tone.loadTestTone(440, 20);
    tone.setLoop(true);
    check("metered voice plays", tone.play());
    check("master meters publish output", waitFor([&] { return engine.getStats().peak > 0; }));
    check("callbacks are healthy before the stall", waitFor([&] { return !engine.getStats().stalled; }));
    internal::pumpAudioDiagnostics();
    const auto stallsBefore = countWarnings("audio stall(s)");
    check("callback parked for stall detection", gate.park());
    check("unfinished callback becomes stalled", waitFor([&] { return engine.getStats().stalled; }));
    const auto stalled = engine.getStats();
    check("stalled meters are zero", stalled.peak == 0 && stalled.rms == 0
          && stalled.cpuUsage == 0 && stalled.cpuUsagePeak == 0);
    const auto voices = engine.getPlayingSounds();
    check("stalled voice level is zero", voices.size() == 1 && voices[0].level == 0);
    const auto state = audioState();
    check("MCP reports stall and counters in the existing layout",
          state.value("stalled", false) && state.value("underrunFrames", uint64_t(0)) == streamUnderruns
          && state["master"]["peak"] == 0 && state["playingSounds"][0]["level"] == 0
          && state.contains("voicesStoppedByReinit") && !state["thread"].contains("overruns"));
    internal::pumpAudioDiagnostics();
    internal::pumpAudioDiagnostics();
    check("a sustained stall is reported once", countWarnings("audio stall(s)") == stallsBefore + 1);
    gate.resume();
    check("completion clears the stall", waitFor([&] { return !engine.getStats().stalled; }));
    tone.stop();

    Sound migrated;
    check("stream for migration starts", bool(migrated.loadStream(path)) && migrated.play());
    migrated.pause(); // paused voices also need a decoder at the new rate
    const auto before = engine.getStats();
    internal::setStreamFaultForTests(internal::StreamFaultForTests::ReopenFails);
    settings.sampleRate = 44100;
    check("live re-init succeeds despite a stream reopen failure", engine.init(settings));
    auto after = engine.getStats();
    check("failed migration stops the voice and counts it separately",
          !migrated.isPlaying() && after.voicesStoppedByReinit == before.voicesStoppedByReinit + 1
          && after.droppedPlays == before.droppedPlays && after.underrunFrames == before.underrunFrames);
    const string migrationWarning = "stream playback migration failed for " + internal::pathToUtf8(path)
        + " (result=-20); stopping the playback"; // MA_IO_ERROR from ReopenFails
    check("migration logs the failed path and result during init", countWarnings(migrationWarning) == 1);
    internal::pumpAudioDiagnostics();
    check("pump adds no migration summary or duplicate",
          countWarnings("stream voices stopped by re-init") == 0 && countWarnings(migrationWarning) == 1);
    check("MCP reports the re-init counter", audioState().value("voicesStoppedByReinit", uint64_t(0)) == after.voicesStoppedByReinit);
    internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    migrated.stop();

    Sound ending;
    check("normal-end stream starts", bool(ending.loadStream(path)) && ending.play());
    ending.setPosition(ending.getDuration() - 0.01f);
    check("normal stream end is reached", waitFor([&] { return !ending.isPlaying(); }));
    const auto atEnd = engine.getStats().underrunFrames;
    check("ended callback parked", gate.park());
    check("normal end silence is excluded", engine.getStats().underrunFrames == atEnd);
    gate.resume();
    ending.stop();

    // Each failed voice logs during init(), even inside the report interval.
    check("second migration stream starts", migrated.play());
    migrated.pause();
    internal::setStreamFaultForTests(internal::StreamFaultForTests::ReopenFails);
    settings.sampleRate = 48000;
    check("second live re-init succeeds", engine.init(settings));
    check("second migration also logs its path and result during init", countWarnings(migrationWarning) == 2);
    out.disconnect();
    engine.waitForAudioCallbacks();
    engine.shutdown();
    check("shutdown adds no migration summary or duplicate",
          countWarnings("stream voices stopped by re-init") == 0 && countWarnings(migrationWarning) == 2);
    after = engine.getStats();
    check("shutdown retains counts and clears stall/meters", !after.stalled && after.peak == 0
          && after.voicesStoppedByReinit == 2 && after.underrunFrames >= underruns);
    { lock_guard<mutex> lock(logMutex); check("every diagnostic warning came from main", logsOnMain); }
    internal::setStreamFaultForTests(internal::StreamFaultForTests::None);
    migrated.stop();
    fs::remove(path);
    return failures ? 1 : 0;
}
