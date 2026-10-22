// Included by the native and Web credit harnesses after check()/drain().
void localFileAdmissionChecks() {
    const char* localPaths[] = {
        "image.png", "./image.png", "../image.png", "/tmp/image.png", "/image.png",
        R"(C:\data\image.png)", "C:/data/image.png", "C:image.png", R"(\data\image.png)",
        "./name:with-colon", "dir/name:with-colon",
        "TrussCResourceCreditUnresolvedFontName", "",
        R"(\\server\share\untrusted.wav)", "//server/share/untrusted.ttf",
        R"(\\?\UNC\server\share\untrusted.png)", R"(//?/UNC/server/share/untrusted.ttf)",
        R"(\\.\pipe\resource)", R"(//./C:/resource.wav)", R"(\\?\C:\resource.wav)",
        R"(\/server\share\resource.wav)", R"(/\server/share/resource.ttf)",
        R"(\??\UNC\server\share\resource.wav)",
        "file://server/share/resource.ttf", "file:///tmp/image.png", "file:/tmp/image.png",
        "data:image/png;base64,abc", "unknown:resource", ":resource", "1:resource",
        "ftp://server/resource.ttf" // not fetched by the current loaders
    };
    for (const auto* input : localPaths) {
        const fs::path path(input);
        check(input, internal::resourceLoadIsLocalPath(path));
        check("equivalent native fs::path stays local", internal::resourceLoadIsLocalPath(path.native()));
        check("relative input resolving to a share or file stays local", internal::resourceLoadIsLocalPath("resource.ttf", path));
        check("local resolution preserves local caller syntax", internal::resourceLoadIsLocalPath(path, "/local/resource.ttf"));
    }
    const char* externalPaths[] = {"http://test/image.png", "https://test/resource.ttf"};
    for (const auto* input : externalPaths) {
        const fs::path path(input);
        check(input, !internal::resourceLoadIsLocalPath(input));
        check("equivalent native fs::path stays external", !internal::resourceLoadIsLocalPath(fs::path(path.native())));
        check("relative input resolved externally stays external", !internal::resourceLoadIsLocalPath("resource.ttf", path));
        check("local resolution cannot erase external caller syntax", !internal::resourceLoadIsLocalPath(path, "/local/resource.ttf"));
    }
    check("ordinary local resolution stays local", internal::resourceLoadIsLocalPath("resource.ttf", "/local/resource.ttf"));
    const auto path = fs::absolute("resourceLoadCredits-local.wav");
    auto writeWave = [&](uint32_t frames) {
        std::ofstream file(path, std::ios::binary);
        auto word = [&](uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) file.put(static_cast<char>(value >> (8 * i)));
        };
        file.write("RIFF", 4); word(36 + frames * 2, 4); file.write("WAVEfmt ", 8);
        word(16, 4); word(1, 2); word(1, 2); word(44100, 4); word(88200, 4);
        word(2, 2); word(16, 2); file.write("data", 4); word(frames * 2, 4);
        for (uint32_t i = 0; i < frames; ++i) word(0, 2);
    };
    int notices = 0;
    auto logger = getLogger().onLog.listen([&](LogEventArgs& message) {
        if (message.level == LogLevel::Notice && message.message.find("[ResourceLoad]") != std::string::npos) ++notices;
    });
    auto batch = [&](int count, size_t byteLimit, bool gateWorkers) {
        internal::setResourceLoadLimitsForTests(4, 64, byteLimit);
        const auto rejected = internal::resourceLoadUsageForTests().rejected;
        int events = 0, errors = 0;
        std::vector<std::unique_ptr<SoundBuffer>> loads;
        std::vector<EventListener> listeners;
#ifndef __EMSCRIPTEN__
        std::mutex mutex;
        std::condition_variable changed;
        int entered = 0;
        bool opened = !gateWorkers;
        internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
            if (std::strcmp(stage, "sound.decode") != 0) return;
            std::unique_lock lock(mutex);
            ++entered; changed.notify_all();
            changed.wait(lock, [&] { return opened; });
        });
#endif
        for (int i = 0; i < count; ++i) {
            auto load = std::make_unique<SoundBuffer>();
            listeners.push_back(load->onLoad.listen([&](LoadResult& result) {
                ++events; if (!result) ++errors;
            }));
            load->loadAsync(path);
            loads.push_back(std::move(load));
        }
        if (gateWorkers) {
#ifndef __EMSCRIPTEN__
            std::unique_lock lock(mutex);
            if (!changed.wait_for(lock, std::chrono::seconds(60), [&] { return entered >= 4; })) std::_Exit(5);
#endif
            const auto usage = internal::resourceLoadUsageForTests();
            check("local files keep four workers and queue beyond 64 pending / 68 live",
                usage.active == 4 && usage.pending == size_t(count - 4) && usage.outstanding == size_t(count));
        }
#ifndef __EMSCRIPTEN__
        { std::lock_guard lock(mutex); opened = true; changed.notify_all(); }
        internal::waitForResourceLoadsForTests();
        internal::setResourceLoadProbeForTests({});
#endif
        size_t peak = internal::resourceLoadUsageForTests().retainedBytes;
        for (int pass = 0; pass < count + 8; ++pass) {
            drain();
            peak = std::max(peak, internal::resourceLoadUsageForTests().retainedBytes);
            if (events == count) break;
        }
        check("every async local-file load completes successfully with zero rejection",
            events == count && errors == 0 && internal::resourceLoadUsageForTests().rejected == rejected);
        check("local application retires all request and byte credits",
            internal::resourceLoadUsageForTests().outstanding == 0 && internal::resourceLoadUsageForTests().retainedBytes == 0);
        return peak;
    };
    writeWave(1);
    batch(74, size_t(1024) * 1024 * 1024, true);
    check("local queue overflow needs no byte Notice", notices == 0);
    writeWave(128 * 1024); // 512 KiB retained PCM per load, 4 MiB per batch.
    const size_t limit = 1024 * 1024;
    check("local retained bytes exceed the lowered budget without discarding results", batch(8, limit, false) > limit);
    check("first local retained-byte overflow logs exactly one Notice", notices == 1);
    batch(8, limit, false);
    check("repeated local overflow stays silent in the same run", notices == 1);
    internal::waitForResourceLoadsAtAppExit();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    batch(8, limit, false);
    check("a new app run gets exactly one new local overflow Notice", notices == 2);
    const auto imagePath = path.parent_path() / "resourceLoadCredits-local.png";
    const auto fontPath = path.parent_path() / "resourceLoadCredits-local.ttf";
    Pixels pixels; pixels.allocate(4, 4, 4);
    check("generated local Image fixture", pixels.save(imagePath).ok());
    const auto fontBytes = makeFont();
    std::ofstream(fontPath, std::ios::binary).write(reinterpret_cast<const char*>(fontBytes.data()), fontBytes.size());
    internal::setResourceLoadLimitsForTests(4, 64, 1);
    Image image;
    Font font;
    int graphicsEvents = 0, graphicsErrors = 0;
    const auto graphicsRejected = internal::resourceLoadUsageForTests().rejected;
    auto graphicsResult = [&](LoadResult& result) { ++graphicsEvents; if (!result) ++graphicsErrors; };
    auto imageResult = image.onLoad.listen(graphicsResult);
    auto fontResult = font.onLoad.listen(graphicsResult);
    image.loadAsync(imagePath); font.loadAsync(fontPath, 19);
    internal::waitForResourceLoadsForTests();
    for (int pass = 0; pass < 8; ++pass) drain();
    check("local Image and Font publish successfully above byte credits",
        graphicsEvents == 2 && graphicsErrors == 0 && image.getPixels().getWidth() == 4 && font.isLoaded() &&
        internal::resourceLoadUsageForTests().retainedBytes == 0 &&
        internal::resourceLoadUsageForTests().rejected == graphicsRejected);
    check("local graphics overflow adds no repeated Notice", notices == 2);
    fs::remove(imagePath); fs::remove(fontPath);
    // File paths and unresolved font names keep real file errors even when
    // no admission bytes are available.
    internal::setResourceLoadLimitsForTests(4, 64, 0);
    Font ambiguous, missingFont;
    Image missingImage;
    SoundBuffer missingSound;
    int nameEvents = 0, missingEvents = 0;
    LoadError nameError = LoadError::None;
    auto ambiguousListener = ambiguous.onLoad.listen([&](LoadResult& result) {
        ++nameEvents; nameError = result.error;
    });
    auto missing = [&](LoadResult& result) { if (result.error == LoadError::FileNotFound) ++missingEvents; };
    auto fontListener = missingFont.onLoad.listen(missing);
    auto imageListener = missingImage.onLoad.listen(missing);
    auto soundListener = missingSound.onLoad.listen(missing);
    ambiguous.loadAsync("TrussCResourceCreditUnresolvedFontName", 16);
    missingFont.loadAsync(path.parent_path() / "TrussCResourceCreditMissing.ttf", 16);
    missingImage.loadAsync(path.parent_path() / "TrussCResourceCreditMissing.png");
    missingSound.loadAsync(path.parent_path() / "TrussCResourceCreditMissing.wav");
    internal::waitForResourceLoadsForTests();
    for (int pass = 0; pass < 8; ++pass) drain();
    check("unresolved font names preserve missing-file failures above admission budgets", nameEvents == 1 && nameError == LoadError::FileNotFound);
    check("local paths preserve real missing-file failures above admission budgets", missingEvents == 3);
    // Exercise the production app-exit drain with a real prepared WAV result.
    // A exits before its Notice is delivered: omission at shutdown is deliberate.
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    int exitNotices = 0, exitEvents = 0;
    auto exitLogger = getLogger().onLog.listen([&](LogEventArgs& message) {
        if (message.level == LogLevel::Notice && message.message.find("[ResourceLoad]") != std::string::npos) ++exitNotices;
    });
    SoundBuffer runA, runB;
    auto exitListener = runA.onLoad.listen([&](LoadResult&) { ++exitEvents; });
    runA.loadAsync(path);
    internal::waitForResourceLoadsForTests();
#ifdef __EMSCRIPTEN__
    drain(); // preparation only; leave the result and Notice for the next frame
#endif
    check("run A has an undelivered local over-budget result", internal::resourceLoadUsageForTests().retainedBytes > 0 && exitNotices == 0);
    internal::appRunToken()->store(false);
    internal::waitForResourceLoadsAtAppExit();
    check("production exit omits run A Notice and callbacks and retires credits", exitNotices == 0 && exitEvents == 0 &&
        internal::resourceLoadUsageForTests().retainedBytes == 0 && internal::resourceLoadUsageForTests().outstanding == 0);
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    runB.loadAsync(path);
    internal::waitForResourceLoadsForTests();
    for (int pass = 0; pass < 8; ++pass) drain();
    check("run B gets exactly one Notice with no stale run A Notice", exitNotices == 1);
    runB.loadAsync(path);
    internal::waitForResourceLoadsForTests();
    for (int pass = 0; pass < 8; ++pass) drain();
    check("run B repeated crossing adds no Notice", exitNotices == 1);
    internal::waitForResourceLoadsAtAppExit();
    fs::remove(path);
    internal::setResourceLoadLimitsForTests(4, 64, size_t(1024) * 1024 * 1024);
}
