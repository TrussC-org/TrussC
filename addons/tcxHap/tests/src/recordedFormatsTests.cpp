#include "tcxHapPlayer.h"
#include <tc/sound/internal/tcAudioDecodeBudget.h>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <atomic>
#include <cstddef>
#if defined(__unix__) && !defined(__EMSCRIPTEN__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tcx::hap {
struct HapBudgetTestAccess {
    static void audio(HapPlayer& player, MovParser&& parser) {
        player.movParser_ = std::move(parser);
        player.audioTrack_ = player.movParser_.getInfo().getVideoTrack();
    }
    static bool loadAudio(HapPlayer& player) { return player.loadAudio(); }
    static bool hasAudio(const HapPlayer& player) { return player.hasAudio_; }
    template<class T> static void resetSteps(const T& samples) { samples.tableSteps_ = 0; }
    template<class T> static uint64_t steps(const T& samples) { return samples.tableSteps_; }
    static auto extract(HapPlayer& player) { return player.getRawAudioData(); }
    static void pcm(HapPlayer& player) {
        auto& track = const_cast<MovTrack&>(*player.audioTrack_);
        track.codecFourCC = FOURCC_SOWT;
        track.channels = 1; track.bitsPerSample = 16; track.sampleRate = 48000;
    }
    static bool loadPcm(HapPlayer& player, tc::SoundBuffer& buffer) {
        return loadPcmTrack(player.movParser_, *player.audioTrack_, buffer);
    }
    static uint64_t rejections(const HapPlayer& player) { return player.rejectedAudio_; }
    static uint64_t charged(const MovParser& parser) { return parser.metadataBytes_ + 256u * 1024; }
    static uint64_t parserRejections(const MovParser& parser) { return parser.rejectedCount_; }
};
}
namespace {
struct alignas(std::max_align_t) Allocation { size_t bytes; };
std::atomic<size_t> liveBytes{0};
bool measuring = false;
size_t largestRequest = 0, failAbove = SIZE_MAX;
void (*allocationHook)(size_t) = nullptr;
bool attachmentArmed = false, attachmentFailed = false;
unsigned attachmentCountdown = 0, attachmentFailAt = 1;
void failAttachmentAllocation(size_t bytes) {
    if (attachmentCountdown && !--attachmentCountdown) {
        allocationHook = nullptr; attachmentFailed = true; throw std::bad_alloc();
    }
    if (attachmentArmed && bytes == sizeof(tc::SoundBuffer) + 2 * sizeof(void*)) {
        attachmentArmed = false; attachmentCountdown = attachmentFailAt;
    }
}
tc::fs::path mutationPath;
size_t mutationOffset = 0, watchedBytes = 0;
uint8_t* guardedAllocation = nullptr;
bool mutationDone = false, guardArmed = false, guardHealthy = true;
void verifyGuard() {
    if (guardedAllocation)
        for (size_t i = 0; i < 64; ++i) guardHealthy &= guardedAllocation[watchedBytes + i] == 0xa5;
}
void mutateOnAllocation(size_t bytes) {
    if (bytes != watchedBytes) return;
    allocationHook = nullptr;
    std::fstream file(mutationPath, std::ios::binary | std::ios::in | std::ios::out);
    const char changed[] = {0, 0, 0, 24};
    file.seekp(static_cast<std::streamoff>(mutationOffset));
    file.write(changed, sizeof(changed)); file.flush();
    mutationDone = bool(file);
    guardArmed = mutationDone;
}
void releaseProbe(void* ptr) noexcept {
    if (ptr && ptr == guardedAllocation) { verifyGuard(); guardedAllocation = nullptr; }
    if (ptr) {
        auto* header = static_cast<Allocation*>(ptr) - 1;
        liveBytes.fetch_sub(header->bytes, std::memory_order_relaxed);
        std::free(header);
    }
}
void* allocateProbe(size_t bytes) {
    if (measuring) {
        largestRequest = std::max(largestRequest, bytes);
        if (bytes > failAbove) { failAbove = SIZE_MAX; throw std::bad_alloc(); }
    }
    if (allocationHook) allocationHook(bytes);
    const bool guarded = guardArmed && bytes == watchedBytes;
    if (void* storage = std::malloc(sizeof(Allocation) + (bytes ? bytes : 1) + (guarded ? 64 : 0))) {
        auto* header = static_cast<Allocation*>(storage);
        header->bytes = bytes;
        liveBytes.fetch_add(bytes, std::memory_order_relaxed);
        void* ptr = header + 1;
        if (guarded) {
            guardArmed = false;
            guardedAllocation = static_cast<uint8_t*>(ptr);
            std::memset(guardedAllocation + watchedBytes, 0xa5, 64);
        }
        return ptr;
    }
    throw std::bad_alloc();
}
using Bytes = std::vector<uint8_t>;
void u32(Bytes& dst, uint32_t value) {
    for (int shift : {24, 16, 8, 0}) dst.push_back(uint8_t(value >> shift));
}
Bytes atom(const char* type, Bytes data) {
    Bytes dst; u32(dst, uint32_t(data.size() + 8));
    dst.insert(dst.end(), type, type + 4); dst.insert(dst.end(), data.begin(), data.end()); return dst;
}
void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }
// All fixtures are generated here; no encoder or downloaded media is required.
Bytes movie(uint32_t samples, uint32_t tableEntries, uint32_t declaredEntries, uint32_t sampleBytes = 12, bool variable = false) {
    Bytes tables, description(36, 0);
    description[3] = 36;
    std::memcpy(description.data() + 4, "Hap1", 4);
    description[33] = 4; description[35] = 4;
    Bytes stsd(4, 0); u32(stsd, 1); append(stsd, description); append(tables, atom("stsd", stsd));
    Bytes stsz(4, 0); u32(stsz, variable ? 0 : sampleBytes); u32(stsz, samples);
    if (variable) for (uint32_t i = 0; i < samples; ++i) u32(stsz, sampleBytes);
    append(tables, atom("stsz", stsz));
    Bytes stsc(4, 0); u32(stsc, 1); u32(stsc, 1); u32(stsc, tableEntries ? std::max(1u, samples / tableEntries) : samples); u32(stsc, 1); append(tables, atom("stsc", stsc));
    Bytes stco(4, 0); u32(stco, declaredEntries);
    for (uint32_t i = 0; i < tableEntries; ++i) u32(stco, 8 + i * 12);
    append(tables, atom("stco", stco));
    Bytes mdia, hdlr(8, 0); u32(hdlr, tcx::hap::HANDLER_VIDE);
    Bytes mdhd(12, 0); u32(mdhd, 60); u32(mdhd, samples);
    append(mdia, atom("mdhd", mdhd));
    append(mdia, atom("hdlr", hdlr));
    append(mdia, atom("minf", atom("stbl", tables)));
    Bytes dst = atom("mdat", Bytes(size_t(samples) * 12, 0));
    // First sample is an uncompressed 4x4 BC1 frame.
    const uint8_t frame[] = {8, 0, 0, 0xab, 0, 0xf8, 0, 0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < samples; ++i) std::memcpy(dst.data() + 8 + size_t(i) * 12, frame, sizeof(frame));
    append(dst, atom("moov", atom("trak", atom("mdia", mdia))));
    return dst;
}
int failures = 0;
void check(const char* name, bool value) {
    std::printf("recorded formats: %-55s %s\n", name, value ? "PASS" : "FAIL");
    if (!value) ++failures;
}
void write(const tc::fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}
void* operator new(size_t bytes) { return allocateProbe(bytes); }
void* operator new[](size_t bytes) { return allocateProbe(bytes); }
void operator delete(void* ptr) noexcept { releaseProbe(ptr); }
void operator delete[](void* ptr) noexcept { releaseProbe(ptr); }
void operator delete(void* ptr, size_t) noexcept { releaseProbe(ptr); }
void operator delete[](void* ptr, size_t) noexcept { releaseProbe(ptr); }

int runRecordedFormatsTests() {
    using namespace tcx::hap;
    const auto path = tc::fs::temp_directory_path() / "tcxHap-recorded-formats.mov";
    write(path, movie(1, 1, 1));
#if defined(__unix__) && !defined(__EMSCRIPTEN__)
    // Isolate the old noexcept-constructor terminate so it is a failing check,
    // rather than terminating the rest of the addon regressions.
    for (unsigned failAt : {1u, 2u}) {
        std::fflush(nullptr);
        const pid_t child = fork();
        if (child == 0) {
            std::set_terminate([] { std::_Exit(86); });
            MovParser audioParser;
            if (!audioParser.open(path)) std::_Exit(87);
            HapPlayer audioPlayer;
            HapBudgetTestAccess::audio(audioPlayer, std::move(audioParser));
            HapBudgetTestAccess::pcm(audioPlayer);
            unsigned errors = 0;
            auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
                if (e.level == tc::LogLevel::Error && e.message.find("Audio loading allocation failed") != std::string::npos) ++errors;
            });
            attachmentArmed = true; attachmentFailAt = failAt;
            allocationHook = failAttachmentAllocation;
            const bool loaded = HapBudgetTestAccess::loadAudio(audioPlayer);
            std::_Exit(!loaded && attachmentFailed && errors == 1 && !HapBudgetTestAccess::hasAudio(audioPlayer) ? 0 : 88);
        }
        int status = 0;
        check("audio attachment constructor allocation fails cleanly",
              child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
#endif
    {
        auto fixture = movie(1, 1, 1);
        const char stsc[] = "stsc", moov[] = "moov";
        const auto run = std::search(fixture.begin(), fixture.end(), stsc, stsc + 4);
        std::fill(run + 16, run + 20, 0); // Zero samples per chunk, nonempty source tables.
        const auto container = std::search(fixture.begin(), fixture.end(), moov, moov + 4);
        const Bytes emptyTrack(container + 4, fixture.end());
        for (unsigned tracks : {100u, 12000u}) {
            Bytes allTracks;
            for (unsigned i = 0; i < tracks; ++i) append(allTracks, emptyTrack);
            Bytes many(fixture.begin(), container - 4);
            append(many, atom("moov", allTracks));
            write(path, many);
            MovParser empty;
            unsigned budgetWarnings = 0;
            auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
                if (e.level == tc::LogLevel::Warning && e.message.find("MOV") != std::string::npos &&
                    (e.message.find("metadata") != std::string::npos || e.message.find("entry capacity") != std::string::npos)) ++budgetWarnings;
            });
            const auto before = liveBytes.load();
            const bool opened = empty.open(path);
            const auto retained = liveBytes.load() - before;
            const auto charged = HapBudgetTestAccess::charged(empty);
            std::printf("recorded formats: empty tracks=%u open=%d retained=%zu charged=%llu warnings=%u\n",
                        tracks, opened, retained, static_cast<unsigned long long>(charged), budgetWarnings);
            check("empty derived views retain at most the metadata budget", retained <= 64u * 1024 * 1024 &&
                  (opened || budgetWarnings == 1));
            check("empty derived view retained bytes are fully charged", charged >= retained);
        }
    }
    for (const auto layout : {std::pair{true, 1u}, std::pair{true, 262144u}, std::pair{false, 262144u}}) {
        const auto [variable, chunks] = layout;
        constexpr size_t count = 262144;
        write(path, movie(count, chunks, chunks, 12, variable));
        MovParser reverse;
        largestRequest = 0; failAbove = 65536; measuring = true;
        const bool reverseOpened = reverse.open(path);
        measuring = false; failAbove = SIZE_MAX;
        check("reverse fixture opens with small allocations", reverseOpened && largestRequest <= 65536);
        if (const auto* video = reverse.getInfo().getVideoTrack()) {
            HapBudgetTestAccess::resetSteps(video->samples);
            uint64_t checksum = 0, expected = 0, maxSteps = 0;
            bool bounded = true;
            for (size_t i = count; i-- > 0;) {
                const auto before = HapBudgetTestAccess::steps(video->samples);
                const auto sample = video->samples[i];
                checksum += sample.offset + sample.size;
                expected += 8 + i * 12 + 12; // Same offsets/sizes as the vector-based parser.
                const uint64_t steps = HapBudgetTestAccess::steps(video->samples) - before;
                maxSteps = std::max(maxSteps, steps);
                bounded &= steps <= 4096;
            }
            std::printf("recorded formats: reverse variable=%d chunks=%u steps=%llu maximum=%llu checksum=%llu\n", variable, chunks,
                        static_cast<unsigned long long>(HapBudgetTestAccess::steps(video->samples)),
                        static_cast<unsigned long long>(maxSteps), static_cast<unsigned long long>(checksum));
            check("reverse sample checksums match the vector index", checksum == expected);
            check("reverse page crossings use at most 4096 table steps", bounded &&
                  HapBudgetTestAccess::steps(video->samples) <= count * 9);
        } else check("reverse track exists", false);
    }
    {
        const auto fixture = movie(1031, 1031, 1031);
        write(path, fixture);
        MovParser lazy;
        check("lazy I/O fixture opens", lazy.open(path));
        unsigned diagnostics = 0;
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
            if ((e.level == tc::LogLevel::Warning || e.level == tc::LogLevel::Error) &&
                e.message.find("MovParser") != std::string::npos) ++diagnostics;
        });
        const char name[] = "stco";
        const auto stco = std::search(fixture.begin(), fixture.end(), name, name + 4);
        tc::fs::resize_file(path, size_t(stco - fixture.begin()) + 16);
        std::vector<uint8_t> bytes{0xff};
        const auto* video = lazy.getInfo().getVideoTrack();
        const bool read = video && lazy.readSample(*video, 0, bytes);
        check("truncated lazy table returns no sample or bytes", !read && bytes.empty());
        check("truncated lazy table emits a diagnostic", diagnostics > 0);
        check("failed lazy source remains failed", video && !lazy.readSample(*video, 0, bytes) && bytes.empty());
    }
    // More than a million samples and table entries stay playable, including
    // the tail and backwards access. The measured allocation limit catches a
    // full table/sample reserve even when eventual parsing would fail.
    write(path, movie(1000001, 1000001, 1000001));
    MovParser parser;
    largestRequest = 0; failAbove = 65536; measuring = true;
    bool opened = false;
    try { opened = parser.open(path); } catch (...) {}
    measuring = false;
    const auto* track = parser.getInfo().getVideoTrack();
    std::printf("recorded formats: largest long-index allocation request %zu bytes\n", largestRequest);
    check("long index opens without a large allocation", opened && largestRequest <= 65536);
    check("long index retains the complete sample count", track && track->samples.size() == 1000001);
    if (track) {
        check("long index tail offset", track->samples.back().offset == 8 + 1000000ull * 12);
        std::vector<uint8_t> bytes;
        HapDecoder tailDecoder; HapDecodedFrame tail;
        check("long recording tail decodes intact", parser.readSample(*track, 1000000, bytes) &&
              tailDecoder.decode(bytes.data(), bytes.size(), 4, 4, tail) && tail.data.size() == 8 && tail.data[1] == 0xf8);
        check("backwards read after tail", parser.readSample(*track, 0, bytes) && bytes.size() == 12 && bytes[3] == 0xab);
    }
    parser.close();
    auto compact = movie(1, 1, 1);
    for (const auto field : {std::pair{"stsz", 12u}, std::pair{"stsc", 16u}}) {
        const auto at = std::search(compact.begin(), compact.end(), field.first, field.first + 4);
        const size_t offset = size_t(at - compact.begin()) + field.second;
        for (unsigned i = 0; i < 4; ++i) compact[offset + i] = uint8_t(1000001u >> (24 - i * 8));
    }
    write(path, compact);
    largestRequest = 0; failAbove = 65536; measuring = true;
    try { opened = parser.open(path); } catch (...) { opened = false; }
    measuring = false;
    track = parser.getInfo().getVideoTrack();
    std::printf("recorded formats: largest compact-index allocation request %zu bytes\n", largestRequest);
    check("compact sample counts do not reserve the whole index", opened && largestRequest <= 65536 && track && track->samples.size() == 1000001);
    if (track) {
        std::vector<uint8_t> tail;
        check("compact out-of-file tail rejects the whole sample", !parser.readSample(*track, 1000000, tail) && tail.empty());
    }
    // Use a fresh parser for independent lifetime diagnostics below.
    parser = MovParser{};
    write(path, movie(1, 0, UINT32_MAX));
    unsigned tableWarnings = 0;
    auto tableListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning && event.message.find("[MovParser]") == 0) ++tableWarnings;
    });
    largestRequest = 0; failAbove = 65536; measuring = true;
    try { opened = parser.open(path); } catch (...) { opened = true; }
    measuring = false;
    check("short table rejected before large allocation", !opened && largestRequest <= 65536);
    check("table rejection repeats cleanly", !parser.open(path));
    check("table losses count across reopen with one Warning", tableWarnings == 1 && HapBudgetTestAccess::parserRejections(parser) == 2);

    HapDecoder decoder;
    HapDecodedFrame decoded;
    {
        // A complete raw BC1 frame at the 128 MiB RGBA boundary.
        Bytes exactFrame(16u * 1024 * 1024 + 8, 0);
        exactFrame[3] = 0xab; exactFrame[7] = 1;
        HapDecodedFrame exact;
        check("complete frame at the RGBA byte limit decodes", decoder.decode(exactFrame.data(), exactFrame.size(), 8192, 4096, exact) &&
              exact.data.size() == 16u * 1024 * 1024 && exact.data.front() == 0 && exact.data.back() == 0);
    }
    Bytes axisFrame(16392 + 4, 0);
    axisFrame[0] = 16392 & 255; axisFrame[1] = 16392 >> 8; axisFrame[3] = 0xab;
    largestRequest = 0; failAbove = 65536; measuring = true;
    bool ok = decoder.decode(axisFrame.data(), axisFrame.size(), 8193, 4, decoded);
    measuring = false;
    check("axis bound checked before decoded allocation", !ok && decoded.data.empty() && largestRequest < 65536);
    write(path, movie(1, 1, 1));
    parser.open(path); track = parser.getInfo().getVideoTrack();
    (void)track->samples[0]; // Separate sample-buffer failure from lazy page creation.
    largestRequest = 0; failAbove = 8; measuring = true;
    std::vector<uint8_t> bytes;
    bool threw = false;
    try { ok = parser.readSample(*track, 0, bytes); } catch (...) { threw = true; }
    measuring = false; failAbove = SIZE_MAX;
    check("sample allocation failure returns cleanly", !ok && !threw && bytes.empty());
    parser.close();
    write(path, movie(1, 1, 1, 64u * 1024 * 1024 + 2));
    tc::fs::resize_file(path, 64u * 1024 * 1024 + 4096);
    check("audio range fixture opens", parser.open(path));
    HapPlayer player;
    HapBudgetTestAccess::audio(player, std::move(parser));
    unsigned warnings = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning) ++warnings;
    });
    largestRequest = 0; failAbove = 65536; measuring = true;
    std::vector<uint8_t> extracted, again;
    bool audioThrew = false;
    try {
        extracted = HapBudgetTestAccess::extract(player);
        again = HapBudgetTestAccess::extract(player);
    } catch (...) { audioThrew = true; }
    measuring = false; failAbove = SIZE_MAX;
    check("audio cap precedes extraction allocation", !audioThrew && extracted.empty() && again.empty() && largestRequest <= 65536);
    check("audio loss counted with one Warning", warnings == 1 && HapBudgetTestAccess::rejections(player) == 2);
    HapBudgetTestAccess::pcm(player);
    auto pcmBytes = HapBudgetTestAccess::extract(player);
    check("full PCM extraction remains complete past encoded cap", pcmBytes.size() == 64u * 1024 * 1024 + 2 && pcmBytes.back() == 0);
    player.close();
    std::vector<uint8_t>().swap(pcmBytes);
    write(path, movie(1, 1, 1));
    parser.open(path);
    HapBudgetTestAccess::audio(player, std::move(parser));
    HapBudgetTestAccess::pcm(player);
    unsigned notices = 0;
    auto noticeListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Notice && event.message.find("decoded audio exceeds") != std::string::npos) ++notices;
    });
    tc::internal::setAudioNoticeThresholdForTests(16);
    tc::SoundBuffer pcm;
    const bool loadedPcm = HapBudgetTestAccess::loadPcm(player, pcm);
    tc::internal::setAudioNoticeThresholdForTests(0);
    check("PCM full load reuses the audio large-load Notice", loadedPcm && pcm.numSamples == 6 && notices == 1);
    player.close();
    auto changingMovie = movie(263, 263, 263, 12, true);
    for (size_t i = 0; i < 263; ++i) changingMovie[8 + i * 12 + 11] = uint8_t(i);
    write(path, changingMovie);
    parser.open(path);
    HapPlayer changing;
    HapBudgetTestAccess::audio(changing, std::move(parser));
    const char sizeType[] = "stsz";
    const auto sizeAt = std::search(changingMovie.begin(), changingMovie.end(), sizeType, sizeType + 4);
    mutationPath = path; mutationOffset = size_t(sizeAt - changingMovie.begin()) + 16;
    watchedBytes = 263 * 12; mutationDone = false; guardHealthy = true;
    allocationHook = mutateOnAllocation;
    auto changedData = HapBudgetTestAccess::extract(changing);
    allocationHook = nullptr; guardArmed = false;
    verifyGuard();
    // A frozen baseline table may finish its original snapshot. A paged table
    // that observes the size change must reject without writing past the buffer.
    check("changing table keeps extraction whole and bounded", guardHealthy &&
          (changedData.empty() || (changedData.size() == watchedBytes &&
           std::equal(changedData.begin(), changedData.end(), changingMovie.begin() + 8))));
    changing.close();
    write(path, changingMovie);
    parser.open(path);
    HapPlayer changingPcm;
    HapBudgetTestAccess::audio(changingPcm, std::move(parser));
    HapBudgetTestAccess::pcm(changingPcm);
    mutationDone = false; guardHealthy = true;
    allocationHook = mutateOnAllocation;
    tc::SoundBuffer changingBuffer;
    const bool changingLoaded = HapBudgetTestAccess::loadPcm(changingPcm, changingBuffer);
    allocationHook = nullptr; guardArmed = false;
    verifyGuard();
    check("changing table keeps PCM destination bounded", guardHealthy &&
          (!changingLoaded || changingBuffer.numSamples == watchedBytes / 2));
    changingPcm.close();
    tc::fs::remove(path);
    return failures;
}
