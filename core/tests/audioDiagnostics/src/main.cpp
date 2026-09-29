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
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
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
        check("path: PCM from memory clears it",
              (bool)reused.loadPcmFromMemory(pcm, sizeof(pcm), 1, 48000) && reused.getPath().empty());
        reused.loadWav(wav);
        reused.generateSineWave(440.0f, 0.1f);
        check("path: a generated wave clears it", reused.getPath().empty());
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
