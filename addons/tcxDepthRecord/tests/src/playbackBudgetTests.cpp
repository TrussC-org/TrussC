#include <tcxDepthRecord.h>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <sstream>
#include <condition_variable>

namespace tcx::depthrecord {
struct PlaybackBudgetTestAccess {
    template<class T> static uint64_t skipped(const T& reader) { return reader.skippedBlocks_; }
};
}
namespace {
bool measuring = false;
size_t largestRequest = 0, failAbove = SIZE_MAX;
void* allocateProbe(size_t bytes) {
    if (measuring) {
        largestRequest = std::max(largestRequest, bytes);
        if (bytes > failAbove) { failAbove = SIZE_MAX; throw std::bad_alloc(); }
    }
    if (void* ptr = std::malloc(bytes ? bytes : 1)) return ptr;
    throw std::bad_alloc();
}
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("playback budget: %-55s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct Probe : tcx::depthrecord::PlaybackDepthCamera {
    using PlaybackDepthCamera::PlaybackDepthCamera;
    struct Reader : PlaybackDevice {
        unsigned calls = 0;
        uint32_t lastLength = 0;
        bool lastZero = false;
        bool readExtraBlock(uint8_t, const uint8_t* data, uint32_t length, double) override {
            ++calls; lastLength = length;
            lastZero = length == 0 || std::all_of(data, data + length, [](uint8_t value) { return value == 0; });
            return true;
        }
    };
};
void recording(const tc::fs::path& path, uint32_t frames, uint32_t customBytes = 0) {
    using namespace tcx::depthrecord;
    tcx::depthcamera::DepthFrame frame;
    frame.w = 4; frame.h = 4;
    auto h = tcd_detail::makeHeader(frame, DepthCodecId::Raw, ColorCodecId::Raw, tcx::depthcamera::DepthSensorType::Unknown);
    h.frameCount = frames;
    h.indexOffset = sizeof(h) + uint64_t(frames) * 8 + (customBytes ? 5ull + customBytes : 0);
    std::ofstream out(path, std::ios::binary);
    tcd_detail::wr(out, h);
    for (uint32_t i = 0; i < frames; ++i) tcd_detail::wr(out, double(i));
    if (customBytes) {
        tcd_detail::wr(out, uint8_t(BLOCK_CUSTOM_BASE)); tcd_detail::wr(out, customBytes);
        out.seekp(static_cast<std::streamoff>(h.indexOffset));
    }
    for (uint32_t i = 0; i < frames; ++i) {
        tcd_detail::wr(out, double(i)); tcd_detail::wr(out, uint64_t(sizeof(h)) + uint64_t(i) * 8);
    }
}
void planeHistory(const tc::fs::path& path, uint32_t frames) {
    using namespace tcx::depthrecord;
    tcx::depthcamera::DepthFrame f; f.w = 8192; f.h = 4096;
    auto h = tcd_detail::makeHeader(f, DepthCodecId::Raw, ColorCodecId::Raw, tcx::depthcamera::DepthSensorType::Unknown);
    h.frameCount = frames; h.streamTypeCount = 2;
    h.streamTypes[0] = BLOCK_DEPTH; h.streamTypes[1] = BLOCK_COLOR;
    constexpr uint32_t pixels = 8192u * 4096, depthBytes = pixels * 2, colorBytes = pixels * 4;
    std::vector<uint64_t> offsets;
    std::ofstream out(path, std::ios::binary);
    out.seekp(sizeof(h));
    for (uint32_t i = 0; i < frames; ++i) {
        offsets.push_back(uint64_t(out.tellp()));
        tcd_detail::wr(out, double(i));
        if (!i) {
            tcd_detail::wr(out, uint8_t(BLOCK_DEPTH)); tcd_detail::wr(out, 12 + depthBytes);
            tcd_detail::wr(out, pixels); tcd_detail::wr(out, depthBytes); tcd_detail::wr(out, depthBytes);
            out.seekp(depthBytes, std::ios::cur);
        } else {
            tcd_detail::wr(out, uint8_t(BLOCK_COLOR)); tcd_detail::wr(out, 17 + colorBytes);
            tcd_detail::wr(out, int32_t(8192)); tcd_detail::wr(out, int32_t(4096)); tcd_detail::wr(out, uint8_t(4));
            tcd_detail::wr(out, colorBytes); tcd_detail::wr(out, colorBytes);
            out.seekp(colorBytes, std::ios::cur);
        }
    }
    h.indexOffset = uint64_t(out.tellp());
    for (uint32_t i = 0; i < frames; ++i) { tcd_detail::wr(out, double(i)); tcd_detail::wr(out, offsets[i]); }
    out.seekp(0); tcd_detail::wr(out, h);
}
// Same deterministic Hilo-LZ4 layout as the review probe. Writer temporaries
// are destroyed before playback, so measured playback allocations exclude them.
size_t hiloRecording(const tc::fs::path& path) {
    using namespace tcx::depthrecord;
    tcx::depthcamera::DepthFrame f; f.w = 8192; f.h = 4096;
    f.depth.resize(size_t(f.w) * f.h);
    uint32_t state = 1;
    for (auto& value : f.depth) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        value = uint16_t(state);
    }
    std::fill(f.depth.end() - 196608, f.depth.end(), uint16_t(0));
    auto h = tcd_detail::makeHeader(f, DepthCodecId::HiloLZ4, ColorCodecId::Raw, tcx::depthcamera::DepthSensorType::Unknown);
    h.frameCount = 3; h.streamTypeCount = 1; h.streamTypes[0] = BLOCK_DEPTH;
    std::ofstream out(path, std::ios::binary);
    tcd_detail::wr(out, h);
    std::vector<uint8_t> scratch, compressed;
    uint64_t offsets[3];
    for (unsigned i = 0; i < 3; ++i) {
        offsets[i] = uint64_t(out.tellp());
        tcd_detail::wr(out, double(i));
        tcd_detail::writeDepthBlock(out, f, DepthCodecId::HiloLZ4, scratch, compressed);
    }
    h.indexOffset = uint64_t(out.tellp());
    for (unsigned i = 0; i < 3; ++i) { tcd_detail::wr(out, double(i)); tcd_detail::wr(out, offsets[i]); }
    out.seekp(0); tcd_detail::wr(out, h);
    return compressed.size();
}
// Gate each capture, so update publishes every frame and all three slots are
// reused deterministically. No sleeps or scheduling/timing assertions.
struct GatedHistoryCamera : tcx::depthrecord::PlaybackDepthCamera {
    std::mutex gate;
    std::condition_variable changed;
    unsigned entered = 0, permitted = 0;
    bool stopping = false;
    std::string path;
    explicit GatedHistoryCamera(std::string input) : PlaybackDepthCamera(input), path(std::move(input)) {}
    ~GatedHistoryCamera() { stop(); }
    struct Reader : Probe::Reader {
        GatedHistoryCamera& owner;
        explicit Reader(GatedHistoryCamera& camera) : owner(camera) {}
        tcx::depthcamera::StreamFreshness captureInto(tcx::depthcamera::DepthFrame& dst) override {
            std::unique_lock lock(owner.gate);
            const unsigned sequence = ++owner.entered;
            owner.changed.notify_all();
            owner.changed.wait(lock, [&] { return owner.stopping || owner.permitted >= sequence; });
            if (owner.stopping) return {};
            lock.unlock();
            return Probe::Reader::captureInto(dst);
        }
    };
    std::shared_ptr<Device> openDevice() override {
        auto reader = std::make_shared<Reader>(*this);
        if (!reader->open(path)) return {};

        return reader;
    }
    void publish(unsigned frame) {
        std::unique_lock lock(gate);
        permitted = frame + 1;
        changed.notify_all();
        changed.wait(lock, [&] { return entered >= frame + 2; });
        lock.unlock();
        update();
    }
    void stop() {
        { std::lock_guard lock(gate); stopping = true; }
        changed.notify_all();
        close();
    }
};

}
void* operator new(size_t bytes) { return allocateProbe(bytes); }
void* operator new[](size_t bytes) { return allocateProbe(bytes); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }

int runPlaybackBudgetTests() {
    using namespace tcx::depthrecord;
    const auto path = tc::fs::temp_directory_path() / "tcxDepthRecord-playback-budget.tcdc";
    const auto hiloBytes = hiloRecording(path);
    check("Hilo fixture matches review payload", hiloBytes == 66980874);
    {
        Probe::Reader hilo;
        tcx::depthcamera::DepthFrame slots[3];
        bool complete = hilo.open(path.string());
        for (unsigned i = 0; i < 6 && complete; ++i) {
            auto& frame = slots[i % 3];
            const auto fresh = hilo.captureInto(frame);
            complete &= fresh.depth && frame.depth.size() == 8192u * 4096;
            uint32_t state = 1;
            if (fresh.depth) for (size_t j = 0; j < frame.depth.size(); ++j) {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                complete &= frame.depth[j] == (j < frame.depth.size() - 196608 ? uint16_t(state) : 0);
            }
        }
        check("large valid Hilo frames decode exactly across three slots", complete);
    }
    // Append a sparse fourth frame: 256 MiB input + 64 MiB output + 64 MiB
    // Hilo planes fits alone, but exceeds 512 MiB with two live 64 MiB slots
    // and playback metadata. Rejection must precede its payload allocation.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        TcdcHeader h{}; tcd_detail::rd(file, h);
        uint64_t offsets[4];
        file.seekg(h.indexOffset);
        for (unsigned i = 0; i < 3; ++i) { double ts; tcd_detail::rd(file, ts); tcd_detail::rd(file, offsets[i]); }
        offsets[3] = h.indexOffset;
        constexpr uint32_t comp = 256u * 1024 * 1024;
        file.seekp(h.indexOffset); tcd_detail::wr(file, double(3));
        tcd_detail::wr(file, uint8_t(BLOCK_DEPTH)); tcd_detail::wr(file, uint32_t(12 + comp));
        tcd_detail::wr(file, uint32_t(8192u * 4096)); tcd_detail::wr(file, uint32_t(64u * 1024 * 1024));
        tcd_detail::wr(file, comp);
        h.frameCount = 4;
        h.indexOffset += 8 + 5 + 12 + comp;
        file.seekp(h.indexOffset);
        for (unsigned i = 0; i < 4; ++i) { tcd_detail::wr(file, double(i)); tcd_detail::wr(file, offsets[i]); }
        file.seekp(0); tcd_detail::wr(file, h);
    }
    {
        Probe::Reader oversized;
        tcx::depthcamera::DepthFrame slots[3];
        bool primed = oversized.open(path.string());
        for (unsigned i = 0; i < 3; ++i) primed &= oversized.captureInto(slots[i]).depth;
        largestRequest = 0; failAbove = 65536; measuring = true;
        const auto fresh = oversized.captureInto(slots[0]);
        measuring = false; failAbove = SIZE_MAX;
        check("over-budget Hilo with live slots rejected before allocation", primed && !fresh.depth && slots[0].depth.empty() &&
              largestRequest <= 65536 && PlaybackBudgetTestAccess::skipped(oversized) == 1);
    }
    planeHistory(path, 3);
    {
        PlaybackDepthCamera camera(path.string());
        camera.enableDepth(); camera.enableColor(); camera.setLoop(false);
        const bool opened = camera.setup();
        bool complete = opened;
        if (opened) for (unsigned i = 0; i < 3; ++i) {
            camera.update();
            complete &= i == 0 ? camera.isFrameNew() : camera.isColorFrameNew();
            if (i) complete &= camera.getColorPixels().getTotalBytes() == 128u * 1024 * 1024;
        }
        check("depth then two color-only frames all play", complete);
    }
    planeHistory(path, 6);
    {
        GatedHistoryCamera camera(path.string());
        camera.enableDepth(); camera.enableColor(); camera.setThreaded(true);
        const bool opened = camera.setup();
        bool complete = opened;
        if (opened) for (unsigned i = 0; i < 6; ++i) {
            camera.publish(i);
            complete &= i == 0 ? camera.isFrameNew() : camera.isColorFrameNew();
            if (i) complete &= camera.getColorPixels().getTotalBytes() == 128u * 1024 * 1024;
        }
        camera.stop();
        check("threaded color-only playback survives depth slot reuse", complete);
    }
    recording(path, 1000001);
    {   // The reader holds the file open; it must be gone before the file is removed (Windows).
    Probe::Reader reader;
    largestRequest = 0; failAbove = 65536; measuring = true;
    bool opened = false;
    try { opened = reader.open(path.string()); } catch (...) {}
    measuring = false;
    std::printf("playback budget: largest index allocation request %zu bytes\n", largestRequest);
    check("long recording opens without large index allocation", opened && largestRequest <= 65536 && reader.getFrameCount() == 1000001);
    tcx::depthcamera::DepthFrame dst;
    if (opened) {
        for (uint32_t i = 0; i < 1000001; ++i) reader.captureInto(dst);
        check("long recording plays through the last index entry", dst.timestamp == 1000000);
        reader.captureInto(dst);
        check("long recording loops to the first frame", dst.timestamp == 0);
    }
    recording(path, 1);
    {
        TcdcHeader shortIndex{};
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        tcd_detail::rd(file, shortIndex); shortIndex.frameCount = UINT32_MAX;
        file.seekp(0); tcd_detail::wr(file, shortIndex);
    }
    largestRequest = 0; failAbove = 65536; measuring = true;
    try { opened = reader.open(path.string()); } catch (...) { opened = true; }
    measuring = false;
    check("index count must fit the file before entries are read", !opened && largestRequest <= 65536 && reader.getFrameCount() == 0);
    recording(path, 1, 64u * 1024 * 1024 + 1);
    check("custom fixture opens", reader.open(path.string()));
    unsigned warnings = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning) ++warnings;
    });
    largestRequest = 0; failAbove = 65536; measuring = true;
    bool threw = false;
    try { reader.captureInto(dst); reader.captureInto(dst); } catch (...) { threw = true; }
    measuring = false;
    check("oversized custom blocks never request payload storage", !threw && largestRequest <= 65536 && reader.calls == 0);
    check("custom block rejection warns once and counts each loss", warnings == 1 && PlaybackBudgetTestAccess::skipped(reader) == 2);

    recording(path, 1, 8);
    check("unadvertised custom fixture opens", reader.open(path.string()));
    reader.captureInto(dst);
    check("unadvertised custom hook still receives complete blocks", reader.calls == 1 && reader.lastLength == 8 && reader.lastZero);

    TcdcHeader h{};
    h.colorCodec = uint8_t(ColorCodecId::Raw);
    std::ostringstream fields(std::ios::binary);
    tcd_detail::wr(fields, int32_t(8193)); tcd_detail::wr(fields, int32_t(1)); tcd_detail::wr(fields, uint8_t(4));
    tcd_detail::wr(fields, uint32_t(8193 * 4)); tcd_detail::wr(fields, uint32_t(8193 * 4));
    std::istringstream input(fields.str(), std::ios::binary);
    std::vector<uint8_t> scratch;
    uint64_t used = 0; const char* why = nullptr;
    largestRequest = 0; failAbove = 65536; measuring = true;
    const bool ok = tcd_detail::parseColorPayload(input, h, 17 + 8193 * 4, 17 + 8193 * 4, dst, scratch, used, why);
    measuring = false; failAbove = SIZE_MAX;
    check("color axis checked before compressed allocation", !ok && scratch.capacity() == 0 && !dst.color.isAllocated() && largestRequest < 8193 * 4);
    std::ostringstream oversizedFields(std::ios::binary);
    const uint32_t colorBytes = 8192u * 4097 * 4, compressedBytes = colorBytes / 255 + 1;
    tcd_detail::wr(oversizedFields, int32_t(8192)); tcd_detail::wr(oversizedFields, int32_t(4097)); tcd_detail::wr(oversizedFields, uint8_t(4));
    tcd_detail::wr(oversizedFields, colorBytes); tcd_detail::wr(oversizedFields, compressedBytes);
    std::istringstream oversizedInput(oversizedFields.str(), std::ios::binary);
    h.colorCodec = uint8_t(ColorCodecId::LZ4);
    largestRequest = 0; failAbove = 65536; measuring = true; threw = false;
    bool admitted = true;
    try { admitted = tcd_detail::parseColorPayload(oversizedInput, h, 17 + compressedBytes, 17 + compressedBytes, dst, scratch, used, why); }
    catch (...) { threw = true; }
    measuring = false; failAbove = SIZE_MAX;
    check("decoded byte bound precedes compressed allocation", !admitted && !threw && largestRequest <= 65536 && scratch.capacity() == 0 && !dst.color.isAllocated());

    std::ostringstream validFields(std::ios::binary);
    tcd_detail::wr(validFields, uint32_t(16)); tcd_detail::wr(validFields, uint32_t(32)); tcd_detail::wr(validFields, uint32_t(32));
    validFields << std::string(32, '\0');
    std::istringstream validInput(validFields.str(), std::ios::binary);
    h.width = h.height = 4; h.depthCodec = uint8_t(DepthCodecId::Raw);
    failAbove = 16; measuring = true; threw = false;
    bool allocated = true;
    try { allocated = tcd_detail::parseDepthPayload(validInput, h, 44, 44, dst, scratch, used, why); }
    catch (...) { threw = true; }
    measuring = false; failAbove = SIZE_MAX;
    check("block allocation failure returns cleanly", !allocated && !threw && dst.depth.empty());
    }
    std::error_code removeError;
    tc::fs::remove(path, removeError);
    return failures;
}
