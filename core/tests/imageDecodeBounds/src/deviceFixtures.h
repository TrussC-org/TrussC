// Test-only device checks. Included after the owned harness helpers.
namespace {
fs::path devicePath;
string deviceExpectation;
bool deviceGpu = false;

Bytes readDeviceFile(const fs::path& path, size_t cap) {
    // Bound (test): external fixtures stop at 64 MiB + 1; manifests at 1 MiB.
    ifstream file(path, ios::binary);
    if (!file) throw runtime_error("cannot open fixture/manifest");
    Bytes result;
    char block[8192];
    while (result.size() < cap && file) {
        const auto count = min(sizeof(block), cap - result.size());
        file.read(block, streamsize(count));
        result.insert(result.end(), block, block + file.gcount());
    }
    if (file.bad() || (file.fail() && !file.eof())) throw runtime_error("fixture/manifest read failed");
    if (result.size() == cap && file.peek() != EOF) throw runtime_error("fixture/manifest exceeds test read bound");
    return result;
}
// Manifest numbers are external input: read them as checked 64-bit values.
bool manifestInt(const Json& v, int64_t lo, int64_t hi, int64_t& out) {
    if (!v.is_number_integer()) return false;
    if (v.is_number_unsigned()) {
        const auto u = v.get<uint64_t>();
        if (u > uint64_t(hi)) return false;
        out = int64_t(u);
    } else out = v.get<int64_t>();
    return out >= lo && out <= hi;
}
bool matchesDevicePixels(const Pixels& pixels, const Json& row) {
    int64_t width = 0, height = 0, channels = 0, tolerance = 0;
    if (!manifestInt(row.at("width"), 1, 65535, width) || !manifestInt(row.at("height"), 1, 65535, height) ||
        !manifestInt(row.at("channels"), 1, 4, channels) ||
        !manifestInt(row.contains("tolerance") ? row.at("tolerance") : Json(0), 0, 255, tolerance))
        throw runtime_error("invalid manifest expectation");
    if (!pixels.isAllocated() || pixels.getWidth() != width || pixels.getHeight() != height ||
        pixels.getChannels() != channels) return false;
    const auto& sample = row.at("sample");
    if (!sample.is_array() || sample.size() < size_t(channels)) throw runtime_error("invalid manifest sample");
    const auto* data = pixels.getData();
    for (int c = 0; c < pixels.getChannels(); ++c) {
        int64_t expected = 0;
        if (!manifestInt(sample.at(c), 0, 255, expected)) throw runtime_error("invalid manifest sample");
        const int64_t diff = int64_t(data[c]) - expected;
        if ((diff < 0 ? -diff : diff) > tolerance) return false;
    }
    return true;
}
void deviceFixture(const fs::path& path, const string& expectation) {
    const auto text = readDeviceFile(path.parent_path() / "manifest.json", 1024 * 1024);
    const auto manifest = Json::parse(text.begin(), text.end());
    const auto& row = manifest.at(path.filename().string()); // Never copy expectation rows.
    const auto bytes = readDeviceFile(path, 64 * 1024 * 1024 + 1);
    for (bool gpu : {false, true}) for (bool memory : {false, true}) {
        Pixels cpu;
        Image image;
        Pixels& target = gpu ? image.getPixels() : cpu;
        target.allocate(3, 2, 4);
        memset(target.getData(), 37, target.getTotalBytes());
        const auto previous = target.clone();
        const auto before = internal::imageDecodeRejectionsForTests();
        const auto result = gpu ? (memory ? image.loadFromMemory(bytes.data(), int(bytes.size())) : image.load(path)) :
                                  (memory ? cpu.loadFromMemory(bytes.data(), int(bytes.size())) : cpu.load(path));
        string expected = expectation;
        if (memory && row.contains("memory")) expected = row.at("memory").get<string>();
        if (expectation == "too-large" && expected != "decode-failed") expected = gpu ? "too-large" : "load";
        if (expectation == "reject-encoded") expected = memory ? "reject-encoded" : "decode-failed";
        const bool load = expected == "load";
        const auto error = load ? LoadError::None : expected == "decode-failed" ? LoadError::DecodeFailed : LoadError::TooLarge;
        const auto losses = expected == "reject-encoded" ? 1 : 0;
        bool ok = bool(result) == load && result.error == error &&
            internal::imageDecodeRejectionsForTests() == before + losses;
        // Synchronous rejection clears the target; the saved previous Pixels stay intact.
        ok = ok && (load ? matchesDevicePixels(target, row) &&
            !(target.getWidth() == 3 && target.getHeight() == 2 && target.getData()[0] == 37) : empty(target));
        ok = ok && previous.getWidth() == 3 && previous.getHeight() == 2 && previous.getChannels() == 4;
        for (size_t i = 0; i < previous.getTotalBytes(); ++i) ok = ok && previous.getData()[i] == 37;
        if (gpu) ok = ok && image.getTexture().isAllocated() == load;
        check(string(gpu ? "Image " : "Pixels ") + (memory ? "memory " : "file ") + path.filename().string(), ok);
    }
}
class ImageDeviceApp : public App {
    void setup() override {
        try {
            check("device GPU context available", sg_isvalid());
            printf("Image device max_image_size_2d: %d\n", internal::imageDecodeAxisLimit());
            if (deviceGpu) {
                if (!devicePath.empty()) {
                    const auto text = readDeviceFile(devicePath / "manifest.json", 1024 * 1024);
                    const auto manifest = Json::parse(text.begin(), text.end());
                    int64_t axisWidth = 0;
                    check("axis fixture matches recorded GPU limit", manifestInt(manifest.at("axis.png").at("width"), 1, 65535, axisWidth) &&
                          axisWidth == internal::imageDecodeAxisLimit());
                    deviceFixture(devicePath / "axis.png", "load");
                    deviceFixture(devicePath / "axis-plus-1.png", "too-large");
                    deviceFixture(devicePath / "axis-y.png", "load");
                    deviceFixture(devicePath / "axis-y-plus-1.png", "too-large");
                }
            } else deviceFixture(devicePath, deviceExpectation);
        } catch (const bad_alloc&) {
            // Allocation-free: report the failure without building a message string.
            ++failures;
            fputs("FAIL: device fixture allocation\n", stdout);
        }
          catch (const exception& e) { printf("Image device fixture input: %s\n", e.what()); check("device fixture input", false); }
        printf("Image device fixture: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
        fflush(stdout);
        exitApp(failures ? 1 : 0);
    }
};
int imageDeviceMain(int argc, char** argv) {
    deviceGpu = string(argv[1]) == "--gpu-limit";
    if (deviceGpu && (argc == 2 || argc == 3)) {
        if (argc == 3) devicePath = fs::absolute(argv[2]);
    } else if (!deviceGpu && argc == 4 && string(argv[1]) == "--device-fixture" &&
               (string(argv[3]) == "load" || string(argv[3]) == "too-large" ||
                string(argv[3]) == "reject-encoded" || string(argv[3]) == "decode-failed")) {
        devicePath = fs::absolute(argv[2]); deviceExpectation = argv[3];
    } else {
        check("device mode arguments", false);
        printf("Image device fixture: FAIL (%d failures)\n", failures);
        return 1;
    }
    WindowSettings settings;
    settings.setSize(64, 64); settings.setTitle("Image device fixture");
    return runApp<ImageDeviceApp>(settings);
}
} // namespace
