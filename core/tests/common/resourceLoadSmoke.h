// Shared native/Web smoke: public loads and ordinary handle lifetimes.
#include "../asyncLoads/src/fontFixture.h"
int failures = 0;
void check(const char* name, bool ok) {
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void settle() {
    internal::waitForResourceLoadsForTests();
#ifdef __EMSCRIPTEN__
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch()); // prepare VFS loads
#endif
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
}
bool resourceReady(const Image& resource) { return resource.isAllocated(); }
bool resourceReady(const SoundBuffer& resource) { return !resource.samples.empty(); }
bool resourceReady(const Font& resource) { return resource.isLoaded(); }
template<class Resource, class Load> void smoke(Load load, const fs::path& path, const fs::path& broken) {
    Resource resource;
    int events = 0;
    bool succeeded = false;
    auto listener = resource.onLoad.listen([&](LoadResult& result) {
        ++events; succeeded = result.ok();
        check("load event is on main with settled loading state", isMainThread() && !resource.isLoading());
    });
    load(resource, path);
    check("load starts with deferred completion", resource.isLoading() && events == 0);
    settle();
    check("load succeeds once", events == 1 && succeeded && resourceReady(resource));
    load(resource, broken); settle();
    check("decoder failure returns a failed load", events == 2 && !succeeded);

    auto owned = make_unique<Resource>();
    int destroyedEvents = 0;
    auto destroyedListener = owned->onLoad.listen([&](LoadResult&) { ++destroyedEvents; });
    load(*owned, path);
    owned.reset(); // pending completion/decoder owns no handle
    settle();
    check("destroying a loading handle suppresses its event", destroyedEvents == 0);

    owned = make_unique<Resource>();
    auto selfListener = owned->onLoad.listen([&](LoadResult& result) {
        check("self-destruction listener receives success", result.ok());
        ++destroyedEvents; owned.reset();
    });
    load(*owned, path); settle();
    check("completion listener can destroy its own handle", !owned && destroyedEvents == 1);
}
int resourceLoadSmoke() {
    getMainThreadId();
    internal::appRunToken() = make_shared<atomic<bool>>(true);
    const auto sandbox = fs::absolute("resource-load-smoke");
    fs::create_directories(sandbox);
    setDataPathRoot(sandbox);
    const auto image = sandbox / "image.ppm", sound = sandbox / "sound.wav";
    const auto font = sandbox / "font.ttf", broken = sandbox / "broken.bin";
    { ofstream file(image, ios::binary); file << "P6\n1 1\n255\n"; file.write("\xff\x00\x00", 3); }
    {
        ofstream file(sound, ios::binary);
        auto word = [&](uint32_t value, int count) {
            for (int i = 0; i < count; ++i) file.put(static_cast<char>(value >> (8 * i)));
        };
        file.write("RIFF", 4); word(38, 4); file.write("WAVEfmt ", 8);
        word(16, 4); word(1, 2); word(1, 2); word(44100, 4); word(88200, 4);
        word(2, 2); word(16, 2); file.write("data", 4); word(2, 4); word(0, 2);
    }
    const auto bytes = makeFont();
    ofstream(font, ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    ofstream(broken) << "not a resource";
    atomic<int> errors{0};
    auto logger = getLogger().onLog.listen([&](LogEventArgs& message) {
        if (message.level == LogLevel::Error) ++errors;
    });
    smoke<Image>([](Image& value, const fs::path& path) { value.loadAsync(path); }, image, broken);
    // Use a recognized extension so the failure exercises the decoder.
    const auto brokenSound = sandbox / "broken.wav";
    fs::copy_file(broken, brokenSound, fs::copy_options::overwrite_existing);
    smoke<SoundBuffer>([](SoundBuffer& value, const fs::path& path) { value.loadAsync(path); }, sound, brokenSound);
    check("sound decoder failure logs an Error", errors > 0);
    const int beforeFont = errors;
    smoke<Font>([](Font& value, const fs::path& path) { value.loadAsync(path, 19); }, font, broken);
    check("font decoder failure logs an Error", errors > beforeFont);
    SoundBuffer source;
    source.loadAsync(sound);
    SoundBuffer copy(source);
    check("copy of loading buffer has an idle destination", !copy.isLoading());
    settle();
    check("copy stays idle after pending work drains", !copy.isLoading() && copy.samples.empty());
    internal::waitForResourceLoadsAtAppExit();
    fs::remove_all(sandbox);
    return failures ? 1 : 0;
}
