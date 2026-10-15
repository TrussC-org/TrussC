#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <cstdlib>
#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace std;
using namespace tc;
namespace {
#include "fontFixture.h"
#include "toneFormats.h"
#include "../../mediaDecode/src/toneOgg.h"
#include "../../aacDecode/src/toneAac.h"
int failures = 0;
void check(const char* name, bool ok) {
    printf("%-76s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++failures;
}
struct Gate {
    mutex lock;
    condition_variable changed;
    int entered = 0;
    bool released = false;
    void block() {
        unique_lock<mutex> held(lock);
        ++entered; changed.notify_all();
        changed.wait(held, [&] { return released; });
    }
    void wait(int count = 1) {
        unique_lock<mutex> held(lock);
        changed.wait(held, [&] { return entered >= count; });
    }
    void open() { lock_guard<mutex> held(lock); released = true; changed.notify_all(); }
    void arrive() { lock_guard<mutex> held(lock); ++entered; changed.notify_all(); }
};
void drain() { internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); }
void finish() { internal::waitForResourceLoadsForTests(); drain(); }
void newRun() {
    if (auto token = internal::appRunToken()) token->store(false);
    internal::appRunToken() = make_shared<atomic<bool>>(true);
}
struct Probe {
    explicit Probe(internal::ResourceLoadProbe fn) { internal::setResourceLoadProbeForTests(std::move(fn)); }
    ~Probe() {
        internal::waitForResourceLoadsForTests();
        internal::setResourceLoadProbeForTests({});
        drain();
    }
};
fs::path png, wav, font, broken, missing, unsupported, fixtureDir;
void fixtures() {
    const fs::path repo = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
    fixtureDir = fs::absolute("asyncLoads-fixtures");
    fs::create_directories(fixtureDir);
    png = fixtureDir / "image.png";
    wav = fixtureDir / "sound.wav";
    font = fixtureDir / "font.ttf";
    broken = fixtureDir / "broken.ttf";
    missing = fixtureDir / "missing.wav";
    unsupported = fixtureDir / "unsupported.xyz";
    fs::copy_file(repo / "core/resources/default.png", png, fs::copy_options::overwrite_existing);
    fs::copy_file(repo / "examples/sound/soundPlayerExample/bin/data/beat_loop.wav", wav, fs::copy_options::overwrite_existing);
    auto data = makeFont();
    ofstream(font, ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
    ofstream(broken) << "not a font or audio or image";
    ofstream(unsupported) << "unsupported";
    setDataPathRoot(fixtureDir);
}
bool sameResult(const LoadResult& a, const LoadResult& b) { return a.error == b.error && a.message == b.message; }

void successAndDelivery() {
    Image image;
    SoundBuffer sound;
    Font typeface;
    int events = 0;
    auto listenImage = image.onLoad.listen([&](LoadResult& r) {
        check("Image applied before main-thread event", r && isMainThread() && !image.isLoading() && image.isAllocated()); ++events;
    });
    auto listenSound = sound.onLoad.listen([&](LoadResult& r) {
        check("Sound applied before main-thread event", r && isMainThread() && !sound.isLoading() && !sound.samples.empty()); ++events;
    });
    auto listenFont = typeface.onLoad.listen([&](LoadResult& r) {
        check("Font applied before main-thread event", r && isMainThread() && !typeface.isLoading() && typeface.isLoaded()); ++events;
    });
    image.loadAsync(png, true); sound.loadAsync(wav); typeface.loadAsync(font, 24);
    internal::waitForResourceLoadsForTests();
    check("all three stay loading until deferred application", events == 0 && image.isLoading() && sound.isLoading() && typeface.isLoading());
    drain();
    check("exactly one event per successful load", events == 3);
    drain(); check("completion is consumed once", events == 3);

    Font cached;
    int cachedEvents = 0;
    auto cacheListener = cached.onLoad.listen([&](LoadResult& r) { check("cache hit succeeds", bool(r)); ++cachedEvents; });
    cached.loadAsync(font, 24);
    check("cache hit never delivers inline", cachedEvents == 0 && cached.isLoading());
    drain(); check("cache hit deferred exactly once", cachedEvents == 1);

    vector<int> order;
    internal::enqueueResourceLoad([&] {
        order.push_back(1);
        internal::enqueueResourceLoad([&] { order.push_back(3); });
    });
    auto batch = internal::takeResourceLoadBatch();
    internal::enqueueResourceLoad([&] { order.push_back(2); });
    internal::deliverResourceLoadBatch(std::move(batch));
    check("snapshot excludes later submissions", order == vector<int>{1});
    drain(); check("drain submissions wait one pass in FIFO order", order == vector<int>({1, 2, 3}));

    // Exercise the real windowed prelude function without a display: jobs
    // queued by another prelude callback wait for a later frame.
    thread submit([&] { runOnMainThread([&] { cached.loadAsync(font, 24); }); });
    submit.join();
    internal::beginMainLoopFrame(chrono::steady_clock::now());
    check("frame snapshot precedes runOnMainThread callbacks", cachedEvents == 1 && cached.isLoading());
    internal::beginMainLoopFrame(chrono::steady_clock::now());
    check("next actual frame delivers the cache hit", cachedEvents == 2);
}

void replacementAndCancel() {
    for (const char* stage : {"image.decode", "image.ready"}) {
        Gate gate;
        atomic<int> visits{0};
        Probe probe([&](const char* at, const fs::path&) { if (string(at) == stage && visits++ == 0) gate.block(); });
        Image image;
        int events = 0;
        auto listener = image.onLoad.listen([&](LoadResult&) { ++events; });
        image.loadAsync(png); gate.wait();
        image.loadAsync(png); // identical request is still a replacement
        check("identical replacement returns while old decoder is gated", image.isLoading() && !gate.released);
        image.loadAsync(broken); // different request, old successes must drop
        image.clear();
        check("clear returns while old decoder is gated", !image.isLoading() && events == 0 && !gate.released);
        image.loadAsync(png);
        gate.open(); finish();
        check("only newest result survives decode/ready replacement", events == 1 && image.isAllocated());
    }
    Gate gate;
    Probe probe([&](const char* stage, const fs::path&) { if (string(stage) == "sound.decode") gate.block(); });
    auto sound = make_unique<SoundBuffer>();
    sound->loadAsync(wav); gate.wait();
    sound.reset();
    check("destruction returns without releasing worker gate", !gate.released && internal::resourceLoadWorkersForTests() >= 1);
    gate.open(); finish();
    check("retirement owner reaps cancelled States", internal::resourceLoadWorkersForTests() == 0);
}

void overrides() {
    // Gate at ready so every overriding synchronous branch is exercised
    // against a fully prepared result, not merely an early cancellation.
    auto imageOverride = [&](auto overrideLoad) {
        Image image; image.load(png); int events = 0;
        auto listener = image.onLoad.listen([&](LoadResult&) { ++events; });
        Gate gate;
        Probe probe([&](const char* stage, const fs::path&) { if (string(stage) == "image.ready") gate.block(); });
        image.loadAsync(png); gate.wait(); overrideLoad(image);
        check("Image synchronous override cancels ready work", !image.isLoading());
        const auto width = image.getWidth(), height = image.getHeight(), channels = image.getChannels();
        vector<unsigned char> pixels;
        if (image.isAllocated()) pixels.assign(image.getPixelsData(), image.getPixelsData() + width * height * channels);
        gate.open(); finish();
        check("Image override keeps pixels/dimensions and silences event", events == 0 && !image.isLoading() &&
              image.getWidth() == width && image.getHeight() == height && image.getChannels() == channels &&
              (pixels.empty() ? !image.isAllocated() : equal(pixels.begin(), pixels.end(), image.getPixelsData())));
    };
    imageOverride([&](Image& x) { x.load(png); });
    imageOverride([&](Image& x) {
        ifstream file(png, ios::binary);
        vector<unsigned char> bytes((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        check("Image memory override succeeds", bool(x.loadFromMemory(bytes.data(), (int)bytes.size())));
    });
    imageOverride([](Image& x) { x.allocate(3, 4); });
    imageOverride([](Image& x) { x.clear(); });
    imageOverride([](Image& x) { x.setColor(0, 0, Color(1, 0, 0)); });
    imageOverride([](Image& x) { x.halve(); });
    imageOverride([](Image& x) { x.resize(3, 4); });
    imageOverride([](Image& x) { x.crop(1, 2, 3, 4); });
    imageOverride([](Image& x) { x.mirror(true, true); });
    imageOverride([](Image& x) { x.mirrorH(); });
    imageOverride([](Image& x) { x.mirrorV(); });
    imageOverride([](Image& x) { x.resize(0, 0); }); // invalid/no-op requests still cancel
    auto soundOverride = [&](auto overrideLoad) {
        SoundBuffer sound; sound.generateSineWave(220, .02f); int events = 0;
        auto listener = sound.onLoad.listen([&](LoadResult&) { ++events; });
        Gate gate;
        Probe probe([&](const char* stage, const fs::path&) { if (string(stage) == "sound.ready") gate.block(); });
        sound.loadAsync(wav); gate.wait(); overrideLoad(sound);
        check("Sound synchronous override cancels ready work", !sound.isLoading());
        const auto samples = sound.samples;
        const auto channels = sound.channels, rate = sound.sampleRate;
        const auto frames = sound.numSamples;
        const auto path = sound.getPath();
        gate.open(); finish();
        check("Sound override keeps PCM/metadata/path and silences event", events == 0 && !sound.isLoading() &&
              sound.samples == samples && sound.channels == channels && sound.sampleRate == rate &&
              sound.numSamples == frames && sound.getPath() == path);
    };
    soundOverride([&](SoundBuffer& x) { x.load(wav); });
    soundOverride([&](SoundBuffer& x) { x.loadWav(wav); });
    soundOverride([&](SoundBuffer& x) { x.loadOgg(broken); });
    soundOverride([&](SoundBuffer& x) { x.loadMp3(broken); });
    soundOverride([&](SoundBuffer& x) { x.loadFlac(broken); });
    soundOverride([&](SoundBuffer& x) { x.loadAac(missing); });
    soundOverride([](SoundBuffer& x) { x.loadWavFromMemory(nullptr, 0); });
    soundOverride([](SoundBuffer& x) { x.loadMp3FromMemory(nullptr, 0); });
    soundOverride([](SoundBuffer& x) { x.loadFlacFromMemory(nullptr, 0); });
    soundOverride([](SoundBuffer& x) { x.loadOggFromMemory(nullptr, 0); });
    soundOverride([](SoundBuffer& x) { check("Sound memory override succeeds", bool(x.loadOggFromMemory(kToneOgg, sizeof(kToneOgg)))); });
    soundOverride([](SoundBuffer& x) { x.loadAacFromMemory(nullptr, 0); });
    soundOverride([](SoundBuffer& x) { x.loadPcmFromMemory(nullptr, 0, 0, 44100); });
    soundOverride([](SoundBuffer& x) { x.generateSineWave(440, .01f); });
    soundOverride([](SoundBuffer& x) { x.generateSquareWave(440, .01f); });
    soundOverride([](SoundBuffer& x) { x.generateTriangleWave(440, .01f); });
    soundOverride([](SoundBuffer& x) { x.generateSawtoothWave(440, .01f); });
    soundOverride([](SoundBuffer& x) { x.generateNoise(.01f); });
    soundOverride([](SoundBuffer& x) { x.generatePinkNoise(.01f); });
    soundOverride([](SoundBuffer& x) { x.generateSilence(.01f); });
    soundOverride([](SoundBuffer& x) { x.applyADSR(.001f, .001f, .5f, .001f); });
    soundOverride([](SoundBuffer& x) { SoundBuffer other; other.generateSineWave(440, .03f); x.mixFrom(other, 0); });
    soundOverride([](SoundBuffer& x) { x.clip(); });
    auto fontOverride = [&](auto overrideLoad) {
        Font typeface; int events = 0;
        auto listener = typeface.onLoad.listen([&](LoadResult&) { ++events; });
        internal::SharedFontCache::getInstance().clear();
        Gate gate;
        Probe probe([&](const char* stage, const fs::path&) { if (string(stage) == "font.ready") gate.block(); });
        typeface.loadAsync(font, 24); gate.wait(); overrideLoad(typeface);
        check("Font synchronous override cancels ready work", !typeface.isLoading());
        gate.open(); finish(); check("Font override silences event", events == 0);
    };
    fontOverride([&](Font& x) { x.load(font, 18); });
    fontOverride([](Font& x) { x.setOversampling(2); });
    fontOverride([](Font& x) { x.setMipmaps(false); });
    fontOverride([](Font& x) { x.clearAtlas(); });
}

template<class Resource, class Start, class Populate> void valueSemantics(Start start, Populate populate) {
    Resource source, destination;
    populate(source); populate(destination);
    int sourceEvents = 0, destinationEvents = 0;
    auto sourceListener = source.onLoad.listen([&](LoadResult&) { ++sourceEvents; });
    auto destListener = destination.onLoad.listen([&](LoadResult&) { ++destinationEvents; });
    Gate gate;
    Probe probe([&](const char* stage, const fs::path&) { if (string(stage).ends_with(".ready")) gate.block(); });
    start(source); start(destination); gate.wait(2);
    Resource copy(source);
    if constexpr (std::is_same_v<Resource, Font>) {
        check("Font copy shares only the committed atlas", copy.getAtlas(0) == source.getAtlas(0));
    } else {
        check("Sound copy owns the committed PCM", copy.samples == source.samples && copy.samples.data() != source.samples.data());
    }
    check("copy starts idle and leaves source pending", !copy.isLoading() && source.isLoading());
    destination = source;
    check("copy assignment cancels only destination", !destination.isLoading() && source.isLoading());
    Resource moved(std::move(destination));
    check("move constructor starts idle", !moved.isLoading());
    destination = std::move(source);
    check("move assignment cancels both pending runs", !source.isLoading() && !destination.isLoading());
    gate.open(); finish();
    check("copies/moves deliver no stale event", sourceEvents == 0 && destinationEvents == 0);
    start(destination); finish();
    check("destination listeners stay with their handle", destinationEvents == 1 && sourceEvents == 0);
    start(copy); finish();
    check("copy has empty listeners", destinationEvents == 1 && sourceEvents == 0);
}
void lifetimeAndReentrancy() {
    atomic<int> errors{0};
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) { if (event.level == LogLevel::Error) ++errors; });
    valueSemantics<SoundBuffer>([](SoundBuffer& x) { x.loadAsync(wav); }, [](SoundBuffer& x) { x.load(wav); });
    internal::SharedFontCache::getInstance().clear();
    valueSemantics<Font>([](Font& x) { x.loadAsync(font, 91); }, [](Font& x) { x.load(font, 20); });
    {
        Gate gate;
        Probe probe([&](const char* stage, const fs::path&) { if (string(stage) == "image.ready") gate.block(); });
        Image source, dest; int events = 0;
        auto listener = source.onLoad.listen([&](LoadResult&) { ++events; });
        source.loadAsync(png); gate.wait(); dest = std::move(source);
        check("Image move cancels without waiting", !source.isLoading() && !dest.isLoading() && !gate.released);
        gate.open(); finish(); check("moved Image drops pending event", events == 0);
    }
    {
        Image image; int events = 0;
        auto listener = image.onLoad.listen([&](LoadResult&) { if (++events == 1) image.loadAsync(png); });
        image.loadAsync(png); finish();
        check("listener can restart its emitter", events == 1 && image.isLoading());
        finish(); check("restarted run has its own later event", events == 2 && !image.isLoading());
    }
    {
        auto emitter = make_unique<Image>(); int events = 0;
        auto first = emitter->onLoad.listen([&](LoadResult&) { ++events; emitter.reset(); });
        auto second = emitter->onLoad.listen([&](LoadResult&) { ++events; });
        emitter->loadAsync(png); finish();
        check("event snapshot survives emitter deletion", !emitter && events == 2);
    }
    {
        SoundBuffer emitter, target; int events = 0;
        auto listener = emitter.onLoad.listen([&](LoadResult&) { ++events; target = std::move(emitter); });
        emitter.loadAsync(wav); finish();
        check("listener can move emitter after application", events == 1 && !target.samples.empty() && !emitter.isLoading());
    }
    check("copy/move/destruction/reentrant listeners add no Error logs", errors == 0);
}

struct Logs {
    vector<string> lines;
    mutex lock;
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& e) {
        lock_guard<mutex> held(lock); lines.push_back(to_string((int)e.level) + ":" + e.message);
    });
};
void parity() {
    for (const fs::path& path : {png, broken, fixtureDir / "missing.png"}) {
        Image sync, async; sync.load(png); async.load(png);
        LoadResult expected, actual;
        vector<string> syncLogs;
        { Logs logs; expected = sync.load(path, true); syncLogs = logs.lines; }
        auto listener = async.onLoad.listen([&](LoadResult& r) { actual = r; });
        vector<string> asyncLogs;
        { Logs logs; async.loadAsync(path, true); finish(); asyncLogs = logs.lines; }
        check("Image sync/async result, logs and populated failure parity", sameResult(expected, actual) && syncLogs == asyncLogs &&
              sync.getWidth() == async.getWidth() && sync.getHeight() == async.getHeight() && sync.isAllocated() == async.isAllocated());
    }
    struct SoundFixture { const char* name; const unsigned char* bytes; size_t size; };
    vector<SoundFixture> clips = {
        {"valid.ogg", kToneOgg, sizeof(kToneOgg)},
        {"valid.flac", toneFlac, sizeof(toneFlac)},
        {"valid.mp3", toneMp3, sizeof(toneMp3)},
    };
#if !defined(__ANDROID__)
    clips.push_back({"valid.m4a", toneAac, sizeof(toneAac)});
#endif
    for (const auto& clip : clips) {
        const fs::path path = fixtureDir / clip.name;
        ofstream(path, ios::binary).write(reinterpret_cast<const char*>(clip.bytes), clip.size);
        SoundBuffer sync, async; sync.generateSilence(.01f); async.generateSilence(.01f);
        LoadResult expected, actual; vector<string> syncLogs, asyncLogs;
        { Logs logs; expected = sync.load(path); syncLogs = logs.lines; }
        auto listener = async.onLoad.listen([&](LoadResult& r) { actual = r; });
        { Logs logs; async.loadAsync(path); finish(); asyncLogs = logs.lines; }
        check("Sound format success has identical result/logs/PCM/metadata/path", expected && sameResult(expected, actual) && syncLogs == asyncLogs &&
              sync.samples == async.samples && sync.channels == async.channels && sync.sampleRate == async.sampleRate &&
              sync.numSamples == async.numSamples && sync.getPath() == async.getPath());
    }
    for (auto ext : {"wav", "ogg", "mp3", "flac", "aac", "m4a", "xyz"}) {
        for (bool exists : {false, true}) {
            fs::path path = fixtureDir / (string(exists ? "bad." : "absent.") + ext);
            if (exists) fs::copy_file(broken, path, fs::copy_options::overwrite_existing);
            SoundBuffer sync, async; sync.load(wav); async.load(wav);
            LoadResult expected, actual; vector<string> syncLogs, asyncLogs;
            { Logs logs; expected = sync.load(path); syncLogs = logs.lines; }
            auto listener = async.onLoad.listen([&](LoadResult& r) { actual = r; });
            { Logs logs; async.loadAsync(path); finish(); asyncLogs = logs.lines; }
            check("Sound format-branch failure preserves sync PCM/metadata/path/logs", sameResult(expected, actual) && syncLogs == asyncLogs &&
                  sync.samples == async.samples && sync.channels == async.channels && sync.sampleRate == async.sampleRate &&
                  sync.numSamples == async.numSamples && sync.getPath() == async.getPath());
        }
    }
    for (const fs::path& path : {font, broken, fixtureDir / "missing.ttf", fs::path("https://invalid/font.ttf")}) {
        Font sync, async; sync.load(font, 24); async.load(font, 24);
        const auto* oldSyncAtlas = sync.getAtlas(0);
        const auto* oldAsyncAtlas = async.getAtlas(0);
        int events = 0;
        LoadResult expected, actual; vector<string> syncLogs, asyncLogs;
        { Logs logs; expected = sync.load(path, 27, path == font ? 2 : 0); syncLogs = logs.lines; }
        auto listener = async.onLoad.listen([&](LoadResult& r) { actual = r; ++events; });
        { Logs logs; async.loadAsync(path, 27, path == font ? 2 : 0); finish(); asyncLogs = logs.lines; }
        check("Font branch-specific result, logs, atlas and logical size parity", sameResult(expected, actual) && syncLogs == asyncLogs &&
              sync.isLoaded() == async.isLoaded() && sync.getAtlasCount() == async.getAtlasCount() &&
              sync.getSize() == async.getSize() && sync.getAscent() == async.getAscent() &&
              sync.getDescent() == async.getDescent() && sync.getWidth("AWa") == async.getWidth("AWa") && events == 1);
        if (path == fs::path("https://invalid/font.ttf")) {
            check("native unsupported URL retains the exact previous atlas in both", !expected &&
                  sync.getAtlas(0) == oldSyncAtlas && async.getAtlas(0) == oldAsyncAtlas && sync.isLoaded() && async.isLoaded() &&
                  sync.getAtlas(0)->getPixels() == async.getAtlas(0)->getPixels());
        } else {
            check("missing/corrupt/invalid-face second load clears both old atlases", !expected &&
                  !sync.isLoaded() && !async.isLoaded() && sync.getAtlas(0) == nullptr && async.getAtlas(0) == nullptr);
        }
    }
    {
        Font sync, async;
        sync.setOversampling(2).setMipmaps(false);
        async.setOversampling(2).setMipmaps(false);
        const auto expected = sync.load(font, 31);
        internal::SharedFontCache::getInstance().clear();
        LoadResult actual;
        auto listener = async.onLoad.listen([&](LoadResult& r) { actual = r; });
        async.loadAsync(font, 31); finish();
        check("Font worker success matches synchronous metrics/options", expected && sameResult(expected, actual) &&
              sync.getSize() == async.getSize() && sync.getAscent() == async.getAscent() && sync.getDescent() == async.getDescent() &&
              sync.getWidth("AWa") == async.getWidth("AWa") && sync.getOversampling() == async.getOversampling() && sync.getMipmaps() == async.getMipmaps());
    }
#ifndef _WIN32
    const auto unreadable = fixtureDir / "unreadable.ttf";
    fs::copy_file(font, unreadable, fs::copy_options::overwrite_existing);
    fs::permissions(unreadable, fs::perms::none);
    Font sync, async; sync.load(font, 24); async.load(font, 24);
    const auto expected = sync.load(unreadable, 29);
    LoadResult actual;
    auto listener = async.onLoad.listen([&](LoadResult& result) { actual = result; });
    async.loadAsync(unreadable, 29);
    check("Font preflight failure stays deferred", async.isLoading() && async.isLoaded());
    finish();
    check("Font file-open failure has synchronous atlas/result/size effects", !expected && sameResult(expected, actual) &&
          sync.isLoaded() == async.isLoaded() && sync.getSize() == async.getSize());
    fs::permissions(unreadable, fs::perms::owner_all);
#endif

}

void fontWorkerRetirement() {
    internal::SharedFontCache::getInstance().clear();
    Gate gate;
    atomic<int> retiredOffMain{0};
    Probe probe([&](const char* stage, const fs::path&) {
        if (string(stage) == "font.ready") gate.block();
        if (string(stage) == "font.cpu-atlas-retired" && !isMainThread()) ++retiredOffMain;
    });
    Font typeface;
    int events = 0;
    auto listener = typeface.onLoad.listen([&](LoadResult&) { ++events; });
    typeface.loadAsync(font, 79); gate.wait();
    typeface.clearAtlas();
    check("Font cancellation returns while prepared CPU candidate is gated", !typeface.isLoading() && !gate.released);
    gate.open(); finish();
    check("cancelled Font candidate takes CPU-only cleanup on worker", retiredOffMain == 1 && !typeface.isLoaded() && events == 0);
}

void pathsAudioAndFontThreading() {
    const fs::path japanese = fixtureDir / utf8ToPath("日本語");
    fs::create_directories(japanese);
    fs::copy_file(png, japanese / "image.png", fs::copy_options::overwrite_existing);
    fs::copy_file(wav, japanese / "sound.wav", fs::copy_options::overwrite_existing);
    fs::copy_file(font, japanese / "sans-serif", fs::copy_options::overwrite_existing);
    setDataPathRoot(japanese);
    Image image; SoundBuffer sound; Font typeface;
    internal::SharedFontCache::getInstance().clear();
    Gate gate;
    atomic<int> cpuOffMain{0};
    Probe probe([&](const char* stage, const fs::path&) {
        if (string(stage).ends_with(".decode")) { if (!isMainThread()) ++cpuOffMain; gate.block(); }
    });
    const auto cwd = fs::current_path();
    image.loadAsync("image.png", true); sound.loadAsync("sound.wav"); typeface.setOversampling(2).setMipmaps(false).loadAsync("sans-serif", 32);
    gate.wait(3);
    setDataPathRoot(fixtureDir / "wrong"); fs::current_path(japanese);
    gate.open(); finish(); fs::current_path(cwd); setDataPathRoot(fixtureDir);
    check("Japanese root/cwd/options are captured at submission", image.isAllocated() && !sound.samples.empty() &&
          sound.getPath() == japanese / "sound.wav" && typeface.isLoaded() && typeface.getOversampling() == 2 && !typeface.getMipmaps());
    check("all native CPU preparation runs off main", cpuOffMain == 3);

    // A data file whose name is also a system family wins, and simultaneous
    // same-key preparation selects the single main-owned cache publication.
    internal::SharedFontCache::getInstance().clear();
    Font first, second;
    first.loadAsync(font, 73); second.loadAsync(font, 73); finish();
    check("concurrent same-key Fonts share one committed atlas", first.isLoaded() && second.isLoaded() && first.getAtlas(0) == second.getAtlas(0));
    const auto collectionPath = fixtureDir / "faces.ttc";
    auto collection = makeTwoFaceCollection();
    ofstream(collectionPath, ios::binary).write(reinterpret_cast<const char*>(collection.data()), collection.size());
    Font faceZero, faceOne, syncFace;
    faceZero.loadAsync(collectionPath, 32, 0); faceOne.loadAsync(collectionPath, 32, 1);
    syncFace.load(collectionPath, 32, 1); finish();
    check("Font collection face/options form distinct cache keys", faceZero.isLoaded() && faceOne.isLoaded() &&
          faceZero.getAtlas(0) != faceOne.getAtlas(0) && faceZero.getWidth("A") != faceOne.getWidth("A") &&
          faceOne.getWidth("A") == syncFace.getWidth("A"));
    auto buffer = make_shared<SoundBuffer>(); buffer->generateSilence(.02f);
    AudioSettings settings; settings.backend = AudioBackend::Null;
    AudioEngine::getInstance().init(settings);
    Sound voice; voice.loadFromBuffer(buffer);
    voice.play();
    buffer->loadAsync(wav); sound.loadAsync(wav); finish();
    check("simultaneous/in-use SoundBuffers load without new failure", !buffer->isLoading() && buffer->samples == sound.samples && buffer->getPath() == wav);
    voice.stop(); shutdownAudio();
}

void runtime() {
    Image image; int events = 0;
    auto listener = image.onLoad.listen([&](LoadResult&) { ++events; });
    image.loadAsync(png); internal::waitForResourceLoadsForTests();
    newRun(); drain();
    check("expired run/hot-reload token drops queued result and event", events == 0 && !image.isAllocated() && !image.isLoading());
    image.loadAsync(png); finish(); check("new run accepts fresh work", events == 1 && image.isAllocated());
}
int headlessEvents = 0;
struct HeadlessExitApp : App {
    Image image;
    EventListener listener;
    void setup() override {
        listener = image.onLoad.listen([](LoadResult&) { ++headlessEvents; });
        image.loadAsync(png);
        internal::waitForResourceLoadsForTests();
        requestExit();
    }
};

// A real sketch: the oldest decode stays gated while 300 frame callbacks
// replace the Image. The gate is opened only after the frame count advanced.
Gate frameGate, imageQueued, fontQueued;
atomic<int> frameDecodeVisits{0};
int replacementFrames = 0;
struct ReplaceEveryFrameApp : App {
    Image image, proofImage, failedImage;
    Font typeface;
    EventListener failureListener;
    bool imageFailureApplied = false;
    EventListener fontListener;
    bool fontApplied = false;
    void setup() override {
        internal::setResourceLoadProbeForTests([](const char* stage, const fs::path&) {
            if (string(stage) == "image.decode" && frameDecodeVisits++ == 2) frameGate.block();
            if (string(stage) == "image.queued") imageQueued.arrive();
            if (string(stage) == "font.queued") fontQueued.arrive();
        });
        proofImage.loadAsync(png, true); imageQueued.wait();
        fontListener = typeface.onLoad.listen([this](LoadResult& result) {
            check("Font GPU/cache application and event stay on main", result && isMainThread() && sg_isvalid());
            fontApplied = true;
        });
        typeface.loadAsync(font, 24); fontQueued.wait();
        Pixels oversized;
        oversized.allocate(sg_query_limits().max_image_size_2d + 1, 1, 4);
        const auto oversizedPath = fixtureDir / "oversized.png";
        check("oversized fixture saved", bool(oversized.save(oversizedPath)));
        Image syncFailure; syncFailure.load(png);
        const auto expected = syncFailure.load(oversizedPath);
        failedImage.load(png);
        failureListener = failedImage.onLoad.listen([this, expected](LoadResult& result) {
            check("Image GPU size failure matches synchronous clearing/result", sameResult(expected, result) &&
                  result.error == LoadError::TooLarge && !failedImage.isAllocated() && !failedImage.isLoading());
            imageFailureApplied = true;
        });
        failedImage.loadAsync(oversizedPath); imageQueued.wait(2);
        image.loadAsync(png); frameGate.wait();
        setFps(VSYNC);
    }
    void update() override {
        image.loadAsync(png);
        if (++replacementFrames == 300) {
            check("300 frame callbacks advance while oldest decode is blocked", !frameGate.released);
            check("asynchronous image upload preserves requested mipmaps", proofImage.getTexture().isAllocated() && proofImage.getTexture().getNumMipLevels() > 1);
            check("asynchronous Font applied through a later frame", fontApplied && typeface.isLoaded());
            check("asynchronous oversized image applied through a later frame", imageFailureApplied);
            frameGate.open();
            exitApp();
        }
    }
    void exit() override {
        frameGate.open();
        internal::waitForResourceLoadsForTests();
        internal::setResourceLoadProbeForTests({});
    }
};

string quote(const string& value) {
#ifdef _WIN32
    return "\"" + value + "\"";
#else
    string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : string(1, c);
    return result + "'";
#endif
}
int childExitCode(int result) {
#ifdef _WIN32
    return result;
#else
    return WIFEXITED(result) ? WEXITSTATUS(result) : 128 + WTERMSIG(result);
#endif
}
int exitChild(const string& mode) {
    newRun();
    auto finished = make_shared<atomic<bool>>(false);
    // The assertion is registered first, so the retirement hook runs before
    // it. Gate opening is an exit hook too: no sleep or elapsed-time check.
    static shared_ptr<atomic<bool>> proof;
    proof = finished;
    atexit([] { if (!proof->load()) std::_Exit(8); });
    static Gate* gate = new Gate;
    internal::startResourceLoad([finished] { gate->block(); finished->store(true); });
    gate->wait();
    atexit([] { gate->open(); });
    if (mode == "worker") {
        internal::startResourceLoad([] { std::exit(0); });
        Gate parked;
        parked.block(); // process exit happens on the worker; main stays alive
        return 7;
    }
    std::exit(0);
}
void exitTests(const char* executable) {
    string command = quote(fs::absolute(executable).string());
#ifdef TC_CORE_TEST_NAME
    command += " " TC_CORE_TEST_NAME;
#endif
    for (auto mode : {"main", "worker"}) {
        fflush(nullptr);
        const int code = childExitCode(system((command + " --exit-child " + mode).c_str()));
        check("process exit drains workers without self-join/abort", code == 0);
    }
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    getMainThreadId();
    if (argc == 3 && string(argv[1]) == "--exit-child") return exitChild(argv[2]);
    newRun(); fixtures();
    if (argc == 2 && string(argv[1]) == "--replace-every-frame") {
        WindowSettings settings; settings.setSize(64, 64); settings.setHighDpi(false);
        runApp<ReplaceEveryFrameApp>(settings);
        check("replacement sketch completed", replacementFrames == 300);
    } else {
        replacementAndCancel(); lifetimeAndReentrancy(); runtime(); exitTests(argv[0]);
        if (!(argc == 2 && string(argv[1]) == "--lifecycle-only")) {
            successAndDelivery(); overrides(); parity(); pathsAudioAndFontThreading(); fontWorkerRetirement();
            runHeadlessApp<HeadlessExitApp>();
            auto discarded = internal::takeResourceLoadBatch();
            check("headless teardown drops queued work before resource teardown", discarded.empty());
            check("headless teardown invalidates its run before delivery", headlessEvents == 0);
        }
    }
    finish();
    fs::remove_all(fixtureDir);
    return failures ? 1 : 0;
}
