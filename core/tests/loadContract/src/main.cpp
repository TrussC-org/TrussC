#include <TrussC.h>
#include "stb/stb_image_write.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>

using namespace tc;
using namespace std;

namespace {
#include "../../mediaDecode/src/toneOgg.h"
#include "../../aacDecode/src/toneAac.h"
int failures = 0;
fs::path dataRoot;
void check(const string& label, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
void writeBytes(const fs::path& path, const void* bytes, size_t size) {
    ofstream f(path, ios::binary);
    f.write(static_cast<const char*>(bytes), size);
    f.close();
    check("fixture written", !f.fail());
}
vector<unsigned char> readBytes(const fs::path& path) {
    ifstream f(path, ios::binary);
    return {istreambuf_iterator<char>(f), istreambuf_iterator<char>()};
}
vector<unsigned char> makeWav() {
    vector<unsigned char> b;
    auto tag = [&](const char* s) { b.insert(b.end(), s, s + 4); };
    auto le = [&](unsigned v, int n) { while (n--) { b.push_back(v & 255); v >>= 8; } };
    tag("RIFF"); le(36 + 32, 4); tag("WAVE"); tag("fmt "); le(16, 4);
    le(1, 2); le(1, 2); le(44100, 4); le(88200, 4); le(2, 2); le(16, 2);
    tag("data"); le(32, 4);
    for (int i = 0; i < 16; ++i) le(i * 100, 2);
    return b;
}
struct ErrorCapture {
    vector<string> messages;
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level == LogLevel::Error) messages.push_back(e.message);
    });
};
template<class F> void missing(const char* label, const fs::path& name, F load) {
    ErrorCapture logs;
    check(string(label) + " fails", !load(name));
    const auto sentence = "file not found: " + internal::pathToDisplayUtf8(dataRoot / name);
    // Module prefixes differ; the sentence (including the resolved name) does not.
    check(string(label) + " logs exactly one shared sentence", logs.messages.size() == 1 &&
          logs.messages[0].ends_with(sentence));
}
void missingFont(Font& font) {
    ErrorCapture logs;
    const fs::path name = "missing/font.ttf";
    const auto result = font.load(name, 16);
    const string sentence = "font not found: \"missing/font.ttf\" (resolved to \"" +
                            internal::pathToUtf8(dataRoot / name) + "\")";
    check("Font missing returns the Font sentence", result.error == LoadError::FileNotFound &&
          result.message == sentence && !font.isLoaded());
    check("Font missing does not log the shared sentence", none_of(logs.messages.begin(), logs.messages.end(),
          [](const string& message) { return message.find("file not found:") != string::npos; }));
}
void imageSizeChecks() {
    const int limit = sg_query_limits().max_image_size_2d;
    Pixels pixels;
    pixels.allocate(limit + 1, 1, 4);
    check("oversize PNG fixture", pixels.save(dataRoot / "large.png").ok());
    Pixels decoded;
    check("Pixels accepts size above GPU limit", decoded.load("large.png").ok() && decoded.getWidth() == limit + 1);
    Image image;
    auto tooLarge = [&](const char* label, int width, int height, const fs::path& name, auto load) {
        ErrorCapture logs;
        const auto result = load();
        const string message = "image exceeds GPU texture limit: " + to_string(limit);
        string logged = "[Image] " + message + " (" + to_string(width) + "x" + to_string(height) + ")";
        if (!name.empty()) logged += ": " + internal::pathToDisplayUtf8(dataRoot / name);
        check(label, result.error == LoadError::TooLarge && result.message == message &&
              !image.isAllocated() && !image.getTexture().isAllocated());
        check(string(label) + " logs exactly one Error", logs.messages.size() == 1 && logs.messages[0] == logged);
    };
    tooLarge("Image rejects width above GPU limit", limit + 1, 1, "large.png", [&] { return image.load("large.png"); });
    auto bytes = readBytes(dataRoot / "large.png");
    tooLarge("Image memory rejects width above GPU limit", limit + 1, 1, {}, [&] { return image.loadFromMemory(bytes.data(), int(bytes.size())); });
    pixels.allocate(1, limit + 1, 4);
    check("tall PNG fixture", pixels.save(dataRoot / "tall.png").ok());
    tooLarge("Image rejects height above GPU limit", 1, limit + 1, "tall.png", [&] { return image.load("tall.png"); });
    pixels.allocate(limit, 1, 4);
    check("boundary PNG fixture", pixels.save(dataRoot / "boundary.png").ok());
    check("Image accepts GPU boundary", image.load("boundary.png").ok() && image.getTexture().isAllocated());
}
void cpuChecks() {
    const auto wav = makeWav();
    writeBytes(dataRoot / "sound.wav", wav.data(), wav.size());
    writeBytes(dataRoot / "sound.ogg", kToneOgg, sizeof(kToneOgg));
    writeBytes(dataRoot / "sound.m4a", toneAac, sizeof(toneAac));
    const char bad[] = "invalid resource";
    writeBytes(dataRoot / "bad.wav", bad, sizeof(bad));
    writeBytes(dataRoot / "bad.ttf", bad, sizeof(bad));
    const string text = "{\"source\":\"data\"}";
    writeBytes(dataRoot / "text.json", text.data(), text.size());
    const string xmlText = "<source>data</source>";
    writeBytes(dataRoot / "text.xml", xmlText.data(), xmlText.size());
    const string iesText = "IESNA:LM-63-2002\nTILT=NONE\n1 1000 2 1 1 1 1 0 0 0 1 1 10\n0 0 42\n";
    writeBytes(dataRoot / "light.ies", iesText.data(), iesText.size());

    check("null audio backend", AudioEngine::getInstance().init(AudioSettings{.backend = AudioBackend::Null}));
    SoundBuffer sound;
    check("SoundBuffer resolves relative path", sound.loadWav("sound.wav").ok() &&
          sound.getPath() == dataRoot / "sound.wav" && sound.numSamples == 16);
    check("decode failure clears name", !sound.loadWav("bad.wav") && sound.getPath().empty());
    using FileLoad = LoadResult (SoundBuffer::*)(const fs::path&);
    const FileLoad fileLoads[] = {&SoundBuffer::load, &SoundBuffer::loadWav, &SoundBuffer::loadMp3,
        &SoundBuffer::loadFlac, &SoundBuffer::loadOgg, &SoundBuffer::loadAac};
    for (auto load : fileLoads) {
        check("seed successful file", sound.loadWav("sound.wav").ok());
        missing("SoundBuffer file entry", "missing.wav", [&](const auto& p) {
            auto r = (sound.*load)(p);
            check("missing classified", r.error == LoadError::FileNotFound);
            return r.ok();
        });
        check("failed entry clears previous name", sound.getPath().empty());
    }
    check("OGG decoded at load with resolved name", sound.loadOgg("sound.ogg").ok() &&
          sound.getPath() == dataRoot / "sound.ogg" && sound.numSamples > 0);
    check("memory success clears name", sound.loadWavFromMemory(wav.data(), wav.size()).ok() && sound.getPath().empty());
    using MemoryLoad = LoadResult (SoundBuffer::*)(const void*, size_t);
    const MemoryLoad memoryLoads[] = {&SoundBuffer::loadWavFromMemory, &SoundBuffer::loadMp3FromMemory,
        &SoundBuffer::loadFlacFromMemory, &SoundBuffer::loadOggFromMemory, &SoundBuffer::loadAacFromMemory};
    for (auto load : memoryLoads) {
        check("seed file before memory failure", sound.loadWav("sound.wav").ok());
        check("memory failure clears name", !(sound.*load)(bad, sizeof(bad)) && sound.getPath().empty());
    }
    check("seed file before PCM failure", sound.loadWav("sound.wav").ok());
    check("invalid PCM clears name", !sound.loadPcmFromMemory(bad, sizeof(bad), 1, 44100, 7) && sound.getPath().empty());
#if !defined(__ANDROID__)
    check("AAC direct load publishes decoded PCM and name", sound.loadAac("sound.m4a").ok() &&
          sound.getPath() == dataRoot / "sound.m4a" && sound.numSamples > 0 && sound.channels > 0);
#if !defined(__EMSCRIPTEN__)
    check("AAC memory success clears name", sound.loadAacFromMemory(toneAac, sizeof(toneAac)).ok() && sound.getPath().empty());
#endif
#endif
#if !defined(__EMSCRIPTEN__)
    SoundStream stream;
    check("stream resolves relative path", stream.loadStream("sound.wav").ok() && stream.getPath() == dataRoot / "sound.wav");
    check("stream decode failure clears name", !stream.loadStream("bad.wav") && stream.getPath().empty());
    check("reload stream", stream.loadStream("sound.wav").ok());
    missing("SoundStream", "missing.wav", [&](const auto& p) { return stream.loadStream(p).ok(); });
    check("stream missing clears name", stream.getPath().empty());
    Sound owner;
    missing("Sound", "missing.wav", [&](const auto& p) { return owner.load(p).ok(); });
    missing("Sound streaming", "missing.wav", [&](const auto& p) { return owner.loadStream(p).ok(); });
#endif
    Pixels source;
    source.allocate(2, 2, 4);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) source.setColor(x, y, Color(1, 0, 0, 1));
    check("PNG fixture", source.save(dataRoot / "image.png").ok());
    const float hdrPixels[3] = {1, 0.5f, 0.25f};
    check("HDR fixture", stbi_write_hdr(internal::pathToUtf8(dataRoot / "image.hdr").c_str(), 1, 1, 3, hdrPixels));
    Pixels px;
    check("Pixels relative data path", px.load("image.png").ok() && px.getWidth() == 2);
    check("Pixels HDR relative data path", px.loadHDR("image.hdr").ok() && px.isFloat());
    Image image;
    check("Image relative data path, headless", image.load("image.png").ok() && image.getWidth() == 2);
    check("text relative data path", loadTextFile("text.json") == text);
    check("JSON relative data path", loadJson("text.json")["source"] == "data");
    FileReader reader;
    check("FileReader relative data path", reader.open("text.json"));
    Xml xml;
    check("XML relative data path", xml.load("text.xml"));
    IesProfile ies;
    missing("Pixels", "missing.png", [&](const auto& p) { return px.load(p).ok(); });
    missing("Pixels platform entry", "missing.png", [&](const auto& p) { return px.loadPlatform(p); });
    missing("FileReader", "missing.txt", [&](const auto& p) { return reader.open(p); });
    missing("Pixels HDR", "missing.hdr", [&](const auto& p) { return px.loadHDR(p).ok(); });
    missing("Image", "missing.png", [&](const auto& p) { return image.load(p).ok(); });
    missing("text", "missing.txt", [&](const auto& p) { return !loadTextFile(p).empty(); });
    missing("JSON", "missing.json", [&](const auto& p) { return !loadJson(p).is_null(); });
    missing("XML", "missing.xml", [&](const auto& p) { return xml.load(p); });
    missing("IES", "missing.ies", [&](const auto& p) { return ies.load(p); });
    Environment env;
    missing("Environment", "missing.hdr", [&](const auto& p) { return env.loadFromHDR(p); });
    VideoPlayer video;
    missing("VideoPlayer", "missing.mp4", [&](const auto& p) { return video.load(p).ok(); });
    check("video failure has empty name", video.getPath().empty());
    check("TooLarge label", string(loadErrorName(LoadError::TooLarge)) == "TooLarge");
    check("Cancelled is failure", !LoadResult::fail(LoadError::Cancelled) && string(loadErrorName(LoadError::Cancelled)) == "Cancelled");
}
#ifdef __EMSCRIPTEN__
template<class F> void timeoutCheck(const char* label, const fs::path& path, F load) {
    ErrorCapture logs;
    const double start = emscripten_get_now();
    const auto result = load();
    const double elapsed = emscripten_get_now() - start;
    const string message = "timed out after 30 s: " + internal::pathToDisplayUtf8(path);
    check(string(label) + " returns Unknown and exact timeout message",
          result.error == LoadError::Unknown && result.message == message);
    check(string(label) + " logs exactly one error", logs.messages.size() == 1 &&
          logs.messages[0].ends_with(message));
    check(string(label) + " waits for the real 30 s deadline", elapsed >= 30000 && elapsed < 35000);
    printf("%s elapsed: %.0f ms\n", label, elapsed);
}

class BrowserAacApp : public App {
    void setup() override {
        // No user gesture or AudioContext.resume(): setup itself must finish decoding.
        const auto path = getDataPath("setup.m4a");
        fs::create_directories(path.parent_path());
        dataRoot = path.parent_path();
        Font missing;
        missingFont(missing);
        imageSizeChecks();
        writeBytes(path, toneAac, sizeof(toneAac));
        SoundBuffer sound;
        check("setup AAC load waits for PCM", sound.loadAac("setup.m4a").ok() &&
              sound.channels > 0 && sound.sampleRate > 0 && sound.numSamples > 0 &&
              sound.samples.size() == sound.numSamples * sound.channels && sound.getPath() == path);
        check("AAC samples are decoded, not silent placeholder", any_of(sound.samples.begin(), sound.samples.end(),
              [](float value) { return abs(value) > 0.001f; }));
        const char bad[] = "invalid AAC";
        writeBytes(getDataPath("invalid.m4a"), bad, sizeof(bad));
        check("setup invalid AAC fails and clears name", sound.loadAac("invalid.m4a").error == LoadError::DecodeFailed && sound.getPath().empty());
        // Optional local HTTP fixtures exercise the other existing URL entries.
        const string fixtureBase = emscripten_run_script_string(
            "new URLSearchParams(location.search).has('url-checks') ? location.origin : ''");
        if (!fixtureBase.empty()) {
            Font font;
            check("setup Font URL returns a usable font", font.load(utf8ToPath(fixtureBase + "/font.ttf"), 16).ok() && font.isLoaded());
            check("Font URL fetch failure is not success", !font.load(utf8ToPath(fixtureBase + "/missing.ttf"), 16) && !font.isLoaded());
            VideoPlayer video;
            const auto videoUrl = utf8ToPath(fixtureBase + "/clip.webm");
            check("setup Video URL returns decoded dimensions and name", video.load(videoUrl).ok() &&
                  video.getWidth() == 16 && video.getHeight() == 16 && video.getPath() == videoUrl);
            check("Video URL first frame is ready at load", video.isReady() && video.getPixels() &&
                  any_of(video.getPixels(), video.getPixels() + 16 * 16 * 4, [](unsigned char value) { return value != 0; }));
            check("Video URL fetch failure clears name", !video.load(utf8ToPath(fixtureBase + "/missing.webm")) && video.getPath().empty());

            if (emscripten_run_script_int("new URLSearchParams(location.search).has('timeout-checks')")) {
                check("seed Font before timeout", font.load(utf8ToPath(fixtureBase + "/font.ttf"), 16).ok());
                const auto fontUrl = utf8ToPath(fixtureBase + "/stall/font.ttf");
                timeoutCheck("Font timeout", fontUrl, [&] { return font.load(fontUrl, 16); });
                check("Font timeout leaves no usable font", !font.isLoaded());
                check("Font reload after timeout", font.load(utf8ToPath(fixtureBase + "/font.ttf"), 16).ok());

                check("seed Video before timeout", video.load(videoUrl).ok());
                const auto stalledVideo = utf8ToPath(fixtureBase + "/stall/clip.webm");
                timeoutCheck("Video timeout", stalledVideo, [&] { return video.load(stalledVideo); });
                check("Video timeout clears name", video.getPath().empty() && !video.isReady());
                check("Video reload after timeout", video.load(videoUrl).ok() && video.getPath() == videoUrl);

                check("seed AAC before timeout", sound.loadAac("setup.m4a").ok());
                // Stall the browser API, without shortening the production deadline.
                // Retain the first decode's resolver to complete it during a later load.
                EM_ASM({
                    var proto = (window.OfflineAudioContext || window.webkitOfflineAudioContext).prototype;
                    window.loadContractDecode = proto.decodeAudioData;
                    proto.decodeAudioData = function() {
                        return new Promise(function(resolve) { window.loadContractLateDecode = resolve; });
                    };
                });
                timeoutCheck("AAC timeout", path, [&] { return sound.loadAac("setup.m4a"); });
                check("AAC timeout clears name", sound.getPath().empty());
                EM_ASM({
                    var proto = (window.OfflineAudioContext || window.webkitOfflineAudioContext).prototype;
                    proto.decodeAudioData = function(bytes) {
                        var context = this;
                        var stale = context.createBuffer(1, 1, 44100);
                        window.loadContractLateDecode(stale);
                        return new Promise(function(resolve, reject) {
                            setTimeout(function() {
                                window.loadContractDecode.call(context, bytes).then(resolve, reject);
                            }, 100);
                        });
                    };
                });
                check("AAC reload ignores late completion of timed-out decode", sound.loadAac("setup.m4a").ok() &&
                      sound.getPath() == path && sound.numSamples > 1 &&
                      any_of(sound.samples.begin(), sound.samples.end(), [](float value) { return abs(value) > 0.001f; }));
                EM_ASM({
                    var proto = (window.OfflineAudioContext || window.webkitOfflineAudioContext).prototype;
                    proto.decodeAudioData = window.loadContractDecode;
                    delete window.loadContractDecode;
                    delete window.loadContractLateDecode;
                });
            }
        }
        EM_ASM({
            document.title = $0 ? "loadContract FAIL" : "loadContract PASS";
            document.body.dataset.loadContract = $0 ? "FAIL" : "PASS";
        }, failures);
        exitApp();
    }
};
#endif
bool gpuCompleted = false;
class LoadApp : public App {
    void setup() override {
        Font font;
        check("Font uses data file before system lookup", font.load("bad.ttf", 16).error == LoadError::DecodeFailed);
        // A slash-containing name cannot be a system family. Keep the Font-specific result.
        missingFont(font);
        const auto fontNames = listSystemFonts();
        bool checkedSystemFont = false;
        for (const auto& name : fontNames) {
            const auto face = internal::systemFontFace(name);
            if (face.path.empty()) continue;
            const auto extension = toLower(getFileExtension(face.path));
            if (extension != "ttf" && extension != "otf" && extension != "ttc" && extension != "otc") continue;
            fs::copy_file(face.path, dataRoot / "font.ttf", fs::copy_options::overwrite_existing);
            check("Font data file", font.load("font.ttf", 16, face.index).ok());
            check("Font system-name fallback", font.load(utf8ToPath(name), 16).ok());
            checkedSystemFont = true;
            break;
        }
        if (!checkedSystemFont) printf("SKIP: no resolvable system font available\n");
        IesProfile ies;
        check("IES relative data path", ies.load("light.ies") && ies.isLoaded());
        imageSizeChecks();
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        const auto movie = fs::path(__FILE__).parent_path() /
            "../../../../addons/tcxHap/tests/bin/data/sine_twos.mov";
        fs::copy_file(movie, dataRoot / "movie.mov", fs::copy_options::overwrite_existing);
        VideoPlayer video;
        video.setUseHwAccel(false);
        check("VideoPlayer relative data path and name", video.load("movie.mov").ok() &&
              video.getPath() == dataRoot / "movie.mov");
        missing("loaded VideoPlayer", "missing.mov", [&](const auto& p) { return video.load(p).ok(); });
        check("VideoPlayer failure clears previous name", video.getPath().empty());
#endif
        Texture texture;
        ErrorCapture logs;
        texture.allocate(sg_query_limits().max_image_size_2d + 1, 1, 4);
        check("direct Texture refuses oversized allocation", !texture.isAllocated() && logs.messages.size() == 1);
        gpuCompleted = true;
        exitApp();
    }
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    WindowSettings settings;
    settings.setSize(32, 32);
    runApp<BrowserAacApp>(settings);
    return failures ? 1 : 0;
#else
    const auto originalCwd = fs::current_path();
    const auto originalRoot = getDataPathRoot();
    const auto sandbox = fs::temp_directory_path() / "tc_loadContract";
    dataRoot = sandbox / fs::path(u8"読み込み");
    fs::create_directories(dataRoot);
    fs::create_directories(sandbox / "cwd");
    setDataPathRoot(dataRoot);
    fs::current_path(sandbox / "cwd");
    cpuChecks();
    bool gpu = argc > 1 && string(argv[1]) == "--gpu-check";
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    gpu = gpu || getenv("DISPLAY") != nullptr;
#endif
    if (gpu) {
        WindowSettings settings;
        settings.setSize(32, 32);
        settings.setHighDpi(false);
        runApp<LoadApp>(settings);
        check("GPU checks completed", gpuCompleted);
    } else printf("SKIP: GPU checks need --gpu-check and a display\n");
    fs::current_path(originalCwd);
    setDataPathRoot(originalRoot);
    fs::remove_all(sandbox);
    return failures ? 1 : 0;
#endif
}
