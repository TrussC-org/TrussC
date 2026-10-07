#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "tc/sound/tcAudioAnalysis.h"
#include <cstdio>
#include <chrono>
#include <thread>

using namespace tc;
using namespace std;
namespace {
int failures = 0;
void check(const char* name, bool ok) {
    printf("%-68s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
json call(const char* name, json args = json::object()) {
    const auto response = json::parse(mcp::Server::instance().processMessage(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
        {"params", {{"name", name}, {"arguments", args}}}}.dump()));
    if (response.contains("error")) return {{"status", "error"}};
    const auto& result = response.at("result");
    const auto text = result.at("content").at(0).at("text").get<string>();
    // A tool that throws answers with MCP's isError result and plain text (#683).
    if (result.value("isError", false)) return {{"status", "error"}, {"message", text}};
    return json::parse(text);
}
void ringChecks() {
    internal::AudioOutputRing ring(64, 2);
    vector<float> signal(300 * 2);
    for (int f = 0; f < 300; ++f) { signal[2*f] = float(f); signal[2*f+1] = float(-f); }
    ring.write(signal.data(), 5, 2);
    auto s = ring.snapshot(64);
    check("startup snapshot has only actual frames", s.framesWritten == 5 && s.samples.size() == 10);
    ring.write(signal.data() + 10, 295, 2);
    s = ring.snapshot(500);
    check("oversized callback retains latest two seconds per channel", s.framesWritten == 300 && s.samples.size() == 256 && s.samples[0] == 172 && s.samples[255] == -299);
    // Small callbacks overlap full two-second reads. Seed history so every
    // read must be nonempty, including the first. Values stay exact in float,
    // and timestamp checks catch a coherent-looking but stale/torn range.
    //
    // No sleeps or timing: the two threads advance in lockstep through two
    // counters. Before snapshot i the reader waits until the writer has
    // written i * kStride frames; the writer starts a block only while it
    // stays within kLead frames of snapshots * kStride. Neither wait can block
    // the other (both stopping would need s*S + L < written < s*S), and every
    // snapshot overlaps the writer's progress. During one snapshot the writer
    // can advance at most kStride + kLead frames, kept below the ring's spare
    // storage (2.5 s stored for a 2 s window = 8192 frames here) so it can
    // never lap the copy, however fast the machine. The deadline only turns
    // a stall (a bug) into a failure instead of a hang; in normal runs it is
    // never reached and does not affect the result.
    constexpr int historyFrames = 32768;
    constexpr int kSnapshots = 30000;
    constexpr uint64_t kStride = 256, kLead = 1024;
    static_assert(kStride + kLead < historyFrames / 4, "writer must not lap a snapshot copy");
    internal::AudioOutputRing concurrent(historyFrames / 2, 2);
    vector<float> seed(historyFrames * 2);
    for (int f = 0; f < historyFrames; ++f) { seed[2*f] = float(f); seed[2*f+1] = -float(f); }
    concurrent.write(seed.data(), historyFrames, 2);
    atomic<bool> readerDone{false}, stalled{false};
    atomic<uint64_t> written{0}, snapshots{0};
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(120);
    auto waitUntil = [&](auto ready) {
        while (!ready()) {
            if (stalled.load(memory_order_relaxed) || chrono::steady_clock::now() > deadline) {
                stalled.store(true, memory_order_relaxed);
                return false;
            }
            this_thread::yield();
        }
        return true;
    };
    thread writer([&] {
        float block[64];
        uint64_t frame = historyFrames;
        // Values must stay exact in float (below 2^24).
        while (frame + 32 < (1u << 24)) {
            const uint64_t done = frame - historyFrames;
            if (!waitUntil([&] { return readerDone.load(memory_order_acquire) ||
                                        done + 32 <= snapshots.load(memory_order_acquire) * kStride + kLead; })) break;
            if (readerDone.load(memory_order_acquire)) break;
            for (int f = 0; f < 32; ++f) { block[2*f] = float(frame + f); block[2*f+1] = -float(frame + f); }
            concurrent.write(block, 32, 2);
            frame += 32;
            written.store(frame - historyFrames, memory_order_release);
        }
    });
    bool nonempty = true, consistent = true;
    uint64_t first = 0, last = 0;
    for (int i = 0; i < kSnapshots; ++i) {
        if (!waitUntil([&] { return written.load(memory_order_acquire) >= i * kStride; })) break;
        s = concurrent.snapshot(historyFrames);
        snapshots.store(i + 1, memory_order_release);
        nonempty &= s.samples.size() == historyFrames * 2;
        if (i == 0) first = s.framesWritten;
        last = s.framesWritten;
        for (size_t f = 0; f < s.samples.size()/2; ++f) {
            const auto frame = s.framesWritten - s.samples.size()/2 + f;
            if (s.samples[2*f] != float(frame) || s.samples[2*f+1] != -float(frame)) consistent = false;
        }
    }
    readerDone.store(true, memory_order_release);
    writer.join();
    // By the last snapshot the writer is at least (kSnapshots - 1) * kStride
    // frames past the seed: far more than three passes over the ring storage.
    const uint64_t wrapTarget = 3 * (historyFrames * 5 / 4);
    check("concurrent reader/writer handshake never stalls", !stalled.load());
    check("30000 concurrent full-history snapshots are never empty", nonempty);
    check("concurrent snapshots are contiguous with consistent channels", consistent);
    check("stress reader overlaps writer progress and multiple wraps", last > first + wrapTarget);
    check("snapshot succeeds once writer is quiescent", concurrent.snapshot(historyFrames).samples.size() == historyFrames * 2);
}
} // namespace

TC_CORE_TEST_MAIN() {
    ringChecks();
    mcp::registerInspectionTools();
    const auto schema = json::parse(mcp::Server::instance().processMessage(
        json{{"jsonrpc","2.0"},{"id",1},{"method","tools/list"}}.dump()));
    for (const auto& t : schema["result"]["tools"]) {
        if (t["name"] == "tc_get_audio_spectrum") check("spectrum schema has exactly the Decision's six arguments",
            t["inputSchema"]["properties"].size() == 6 && t["inputSchema"]["properties"].contains("fmin") && t["inputSchema"]["properties"].contains("peaks"));
        if (t["name"] == "tc_save_audio_capture") check("capture schema has only path and seconds; path is required",
            t["inputSchema"]["properties"].size() == 2 && t["inputSchema"]["required"] == json::array({"path"}));
    }
    auto& engine = AudioEngine::getInstance();
    check("spectrum without engine returns an error", call("tc_get_audio_spectrum")["status"] == "error");
    check("capture without engine returns an error", call("tc_save_audio_capture", {{"path","never.wav"}})["status"] == "error");
    check("tools never start audio", !engine.isInitialized());
    internal::setNullAudioBackendForTests(true);
    auto constant = engine.audioOut.listen([](AudioOutBuffer& out) {
        for (int f = 0; f < out.frameCount; ++f) { out.data[f*2] = 0.25f; out.data[f*2+1] = -0.125f; }
    });
    check("Null backend starts", engine.init(AudioSettings{.sampleRate=48000, .channels=2, .bufferSize=256}));
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    while (internal::AudioAnalysisAccess::snapshot(engine, 0).framesWritten < 4096 && chrono::steady_clock::now() < deadline) this_thread::yield();
    vector<float> mono(4096);
    const size_t copied = engine.getAnalysisBuffer(mono.data(), mono.size());
    check("legacy mono API reads the live output ring", copied == 4096 && all_of(mono.begin(), mono.end(), [](float x) { return x == 0.0625f; }));
    vector<float> capped(5000, 42.0f);
    check("legacy mono request stays capped at ANALYSIS_BUFFER_SIZE",
        engine.getAnalysisBuffer(capped.data(), capped.size()) == AudioEngine::ANALYSIS_BUFFER_SIZE &&
        all_of(capped.begin(), capped.begin() + 4096, [](float x) { return x == 0.0625f; }) && capped[4096] == 42.0f);
    bool liveCopies = true;
    for (int i = 0; i < 30000; ++i) {
        liveCopies &= engine.getAnalysisBuffer(mono.data(), mono.size()) == mono.size();
        liveCopies &= all_of(mono.begin(), mono.end(), [](float x) { return x == (0.25f - 0.125f) * 0.5f; });
    }
    check("30000 live mono copies retain the old stereo mix without empty reads", liveCopies);
    engine.shutdown();
    check("legacy mono API returns zero when stopped", engine.getAnalysisBuffer(mono.data(), mono.size()) == 0);
    constant.disconnect();
    // With the device stopped, drive the same mixer synchronously. No sleeps,
    // callback timing assumptions or hardware are involved in numeric checks.
    uint64_t phase = 0;
    int mode = 0;
    uint32_t random = 1;
    auto listener = engine.audioOut.listen([&](AudioOutBuffer& out) {
        for (int f = 0; f < out.frameCount; ++f, ++phase) {
            float left = 0, right = 0;
            if (mode == 0) { left = float(0.5 * sin(double(TAU) * 440 * phase / 48000)); right = float(0.25 * sin(double(TAU) * 880 * phase / 48000)); }
            if (mode == 1) { left = 2; right = -2; }
            if (mode == 3) { random = 1664525 * random + 1013904223; left = right = float(double(random) / UINT32_MAX - 0.5); }
            out.data[2*f] = left; out.data[2*f+1] = right;
        }
    });
    vector<float> output(100000 * 2);
    engine.mixAudio(output.data(), 100000, 2);
    auto r = call("tc_get_audio_spectrum");
    check("default result has only specified metadata and mono channel", r.size() == 5 && r["n"] == 1024 && r["binHz"] == 46.875 && r["channels"].size() == 1 && r["channels"][0].size() == 4 && r["channels"][0]["spectrum"].size() == 513);
    for (const auto* window : {"hann", "blackmanharris"}) {
        r = call("tc_get_audio_spectrum", {{"n",8192},{"channels","each"},{"window",window}});
        const auto& left = r["channels"][0]; const auto& right = r["channels"][1];
        check(window, abs(left["peaks"][0]["hz"].get<double>() - 440) < 0.5 && abs(left["peaks"][0]["dbfs"].get<double>() + 6.0) <= 0.11 && abs(right["peaks"][0]["hz"].get<double>() - 880) < 0.5 && abs(right["peaks"][0]["dbfs"].get<double>() + 12.0) <= 0.11);
        check("linear peak/RMS and full spectrum", abs(left["peak"].get<double>() - 0.5) < 0.001 && abs(left["rms"].get<double>() - sqrt(0.125)) < 0.002 && left["spectrum"].size() == 4097);
    }
    const auto full = call("tc_get_audio_spectrum", {{"n",1024},{"peaks",0}});
    r = call("tc_get_audio_spectrum", {{"n",1024},{"fmin",400},{"fmax",900},{"peaks",0}});
    json expected = json::array();
    for (int k = 9; k <= 19; ++k) expected.push_back(full["channels"][0]["spectrum"][k]);
    check("frequency selection returns inclusive matching bins and no peaks", r["channels"][0]["spectrum"] == expected && r["channels"][0]["peaks"].empty());
    check("minimum and maximum power-of-two sizes", call("tc_get_audio_spectrum",{{"n",64}})["channels"][0]["spectrum"].size()==33 && call("tc_get_audio_spectrum",{{"n",65536}})["channels"][0]["spectrum"].size()==32769);
    bool invalid = true;
    for (const auto& args : vector<json>{{{"n",63}},{{"n",96}},{{"n",131072}},{{"n",64.5}},{{"n",uint64_t(-1)}},{{"window","blackman"}},{{"channels","left"}},{{"peaks",-1}},{{"peaks",1.5}},{{"fmin",-1}},{{"fmax",24001}},{{"fmin",900},{"fmax",400}}}) invalid &= call("tc_get_audio_spectrum",args)["status"] == "error";
    check("invalid spectrum arguments are rejected", invalid);
    const auto dir = fs::current_path()/"mcpAudio-output";
    fs::create_directories(dir);
    setDataPathRoot(dir);
    r = call("tc_save_audio_capture", {{"path","nested/音声.capture"},{"seconds",0.1}});
    SoundBuffer decoded;
    check("capture reports only Decision fields and actual appended path", r.size() == 5 && r["frames"] == 4800 && r["channels"] == 2 && r["sampleRate"] == 48000 && r["path"] == internal::pathToUtf8(dir/"nested/音声.capture.wav"));
    {
        ifstream file(internal::utf8ToPath(r["path"].get<string>()), ios::binary);
        const string bytes((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        const size_t fmt = bytes.find("fmt ");
        uint16_t format = 0, bits = 0;
        if (fmt != string::npos && fmt + 24 <= bytes.size()) {
            memcpy(&format, bytes.data() + fmt + 8, 2);
            memcpy(&bits, bytes.data() + fmt + 22, 2);
        }
        check("WAV header declares IEEE float32", format == 3 && bits == 32);
    }
    check("float WAV decodes", bool(decoded.load(internal::utf8ToPath(r["path"].get<string>()))));
    check("float WAV preserves exact last samples and channel order", decoded.samples.size() == 9600 && equal(decoded.samples.begin(), decoded.samples.end(), output.end() - 9600));
    r = call("tc_save_audio_capture", {{"path",internal::pathToUtf8(dir/"absolute.WAV")},{"seconds",100}});
    check("absolute WAV path stays unchanged; long request caps at ring", r["frames"] == 96000 && r["path"] == internal::pathToUtf8(dir/"absolute.WAV") && r["framesWritten"] == full["framesWritten"]);
    check("capture uses existing history without advancing frames", call("tc_get_audio_spectrum")["framesWritten"] == full["framesWritten"]);
    check("invalid capture arguments fail", call("tc_save_audio_capture",{{"path",""}})["status"] == "error" && call("tc_save_audio_capture",{{"path","bad"},{"seconds",-1}})["status"] == "error" && call("tc_save_audio_capture",{{"path","bad"},{"seconds",0.000001}})["status"] == "error" && call("tc_save_audio_capture")["status"] == "error");
    ofstream(dir/"file").put('x');
    check("file errors are reported", call("tc_save_audio_capture",{{"path","file/child.wav"}})["status"] == "error");
    mode = 1; engine.mixAudio(output.data(), 100000, 2);
    r = call("tc_get_audio_spectrum",{{"channels","each"}});
    check("history is post-clamp; DC is not doubled", r["channels"][0]["peak"] == 1 && r["channels"][0]["rms"] == 1 && r["channels"][0]["spectrum"][0] == 0);
    check("mix averages channels", call("tc_get_audio_spectrum")["channels"][0]["peak"] == 0);
    mode = 2; engine.mixAudio(output.data(), 100000, 2);
    r = call("tc_get_audio_spectrum");
    check("silence returns null dBFS and no peaks", r["channels"][0]["peaks"].empty() && all_of(r["channels"][0]["spectrum"].begin(),r["channels"][0]["spectrum"].end(),[](const json& x){return x.is_null();}));
    mode = 3; engine.mixAudio(output.data(), 100000, 2);
    r = call("tc_get_audio_spectrum",{{"n",8192}});
    check("seeded noise has broadband energy", r["channels"][0]["rms"].get<double>() > 0.27 && r["channels"][0]["peaks"][0]["dbfs"].get<double>() < -20);
    listener.disconnect();
    check("reinit to mono/new rate", engine.init(AudioSettings{.sampleRate=32000,.channels=1,.bufferSize=256}));
    engine.shutdown();
    r = call("tc_get_audio_spectrum");
    check("reinit clears old history and updates format", r["sampleRate"] == 32000 && r["framesWritten"].get<uint64_t>() < 100000 && r["channels"][0]["peak"] == 0);
    auto startup = engine.audioOut.listen([](AudioOutBuffer& out) {
        std::fill_n(out.data, out.frameCount * out.channels, 0.25f);
    });
    float shortOutput[32];
    engine.mixAudio(shortOutput, 32, 1);
    r = call("tc_get_audio_spectrum");
    check("startup spectrum zero-pads missing samples", r["channels"][0]["peak"] == 0.25 &&
        abs(r["channels"][0]["rms"].get<double>() - 0.25 * sqrt(32.0 / 1024)) < 1e-8);
    const auto saved = call("tc_save_audio_capture", {{"path","startup.wav"},{"seconds",2}});
    check("startup capture writes only available history without padding", saved["frames"] == r["framesWritten"] && saved["frames"].get<size_t>() < 64000);
    startup.disconnect();
    // A periodic ramp makes reversed ordering visible. With three channels,
    // the third channel must not participate in the legacy L/R average.
    for (int channels : {1, 3}) {
        auto ramp = engine.audioOut.listen([](AudioOutBuffer& out) {
            for (int f = 0; f < out.frameCount; ++f) {
                const float x = float((out.framePosition + f) % 1024) / 2048.0f;
                out.data[f * out.channels] = x;
                if (out.channels > 1) out.data[f * out.channels + 1] = x * 0.5f;
                if (out.channels > 2) out.data[f * out.channels + 2] = -1.0f;
            }
        });
        const bool started = engine.init(AudioSettings{.sampleRate=48000,.channels=channels,.bufferSize=256});
        const auto readyBy = chrono::steady_clock::now() + chrono::seconds(10);
        while (internal::AudioAnalysisAccess::snapshot(engine, 0).framesWritten < 4096 && chrono::steady_clock::now() < readyBy) this_thread::yield();
        const auto n = engine.getAnalysisBuffer(mono.data(), mono.size());
        const float scale = channels == 1 ? 1.0f : 0.75f;
        const int first = int(mono[0] * 2048.0f / scale);
        bool ordered = started && n == mono.size();
        for (size_t f = 0; f < n; ++f) ordered &= mono[f] == float((first + f) % 1024) / 2048.0f * scale;
        check(channels == 1 ? "legacy mono preserves samples in oldest-first order" :
            "legacy multichannel uses only L/R in oldest-first order", ordered);
        engine.shutdown();
        ramp.disconnect();
    }
    internal::setNullAudioBackendForTests(false);
    fs::remove_all(dir);
    return failures ? 1 : 0;
}
