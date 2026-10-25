#include <TrussC.h>
#include "tc/sound/internal/tcAudioDecodeBudget.h"
#include "../../common/tcCoreTest.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include "miniaudio.h"
#include "tc/sound/internal/tcFFmpegAudio.h"
#endif
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#include <cstdio>
#include <fstream>
#include <vector>
#include <limits>
#include <chrono>
#include <thread>
#include <iterator>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>

#if defined(__linux__) && !defined(__ANDROID__)
void audioMallocProbeArm();
size_t audioMallocProbeDisarm();
#endif

namespace {
using namespace std;
using namespace tc;
#include "../../audioDiagnostics/src/vorbisTone.cpp"
#if defined(__linux__) && !defined(__ANDROID__)
#include "../../aacDecode/src/toneAac.h"
#endif
int failures = 0;
void check(const char* label, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
vector<char> wavBytes(unsigned frames, unsigned channels = 1, unsigned rate = 48000) {
    vector<char> bytes;
    auto text = [&](const char* p, size_t n) { bytes.insert(bytes.end(), p, p + n); };
    auto u16 = [&](unsigned v) { bytes.push_back(char(v)); bytes.push_back(char(v >> 8)); };
    auto u32 = [&](unsigned v) { for (int i = 0; i < 4; ++i) bytes.push_back(char(v >> (8*i))); };
    text("RIFF", 4); u32(36 + frames * channels * 2); text("WAVEfmt ", 8);
    u32(16); u16(1); u16(channels); u32(rate); u32(rate * channels * 2);
    u16(channels * 2); u16(16); text("data", 4); u32(frames * channels * 2);
    for (unsigned i = 0; i < frames * channels; ++i) u16(8192);
    return bytes;
}
static vector<char> flacBytes(int blocks, uint64_t statedSamples, bool changeRate = false) {
    vector<uint8_t> out = {'f', 'L', 'a', 'C', 0x80, 0x00, 0x00, 34};   // last block: STREAMINFO
    // STREAMINFO: min / max block size 4096, min / max frame size unknown,
    // then 64 bits of sample rate (20), channels - 1 (3), bits - 1 (5) and
    // total samples (36)
    vector<uint8_t> si = {0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0};
    const uint64_t packed = ((uint64_t)48000 << 44) | ((uint64_t)15 << 36) |
                            (statedSamples & 0xFFFFFFFFFull);
    for (int i = 0; i < 8; ++i) si.push_back((uint8_t)(packed >> (56 - 8 * i)));
    out.insert(out.end(), si.begin(), si.end());
    out.insert(out.end(), 16, 0);   // MD5 unknown
    auto crc8 = [](const uint8_t* p, size_t n) {
        uint8_t c = 0;
        for (size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int b = 0; b < 8; ++b) c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
        }
        return c;
    };
    auto crc16 = [](const uint8_t* p, size_t n) {
        uint16_t c = 0;
        for (size_t i = 0; i < n; ++i) {
            c ^= (uint16_t)(p[i] << 8);
            for (int b = 0; b < 8; ++b) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1);
        }
        return c;
    };
    for (int f = 0; f < blocks; ++f) {
        // Valid header/frame CRCs, fixed blocks of 4096, mono / 16-bit.
        // Optional second frame switches from 48 kHz to 44.1 kHz (both valid rates).
        vector<uint8_t> fr = {0xFF, 0xF8, uint8_t(changeRate && f > 0 ? 0xC9 : 0xCA), 0x08, (uint8_t)f};
        fr.push_back(crc8(fr.data(), fr.size()));
        fr.insert(fr.end(), {0x00, 0x03, 0xE8});   // CONSTANT subframe: 1000
        const uint16_t c = crc16(fr.data(), fr.size());
        fr.push_back((uint8_t)(c >> 8));
        fr.push_back((uint8_t)c);
        out.insert(out.end(), fr.begin(), fr.end());
    }
    return vector<char>(out.begin(), out.end());
}


bool intact(const SoundBuffer& b) {
    return b.channels == 1 && b.sampleRate == 48000 && b.numSamples == 1 &&
        b.samples.size() == 1 && b.samples[0] == 0.25f;
}
void writeBytes(const fs::path& path, const vector<char>& bytes) {
    ofstream f(path, ios::binary); f.write(bytes.data(), streamsize(bytes.size()));
}
void asyncReplacement(const fs::path& path) {
    const auto token = internal::appRunToken();
    internal::appRunToken() = make_shared<atomic<bool>>(true);
    internal::setAudioWorkingLimitForTests(1); // affects streaming only
    auto run = [&](size_t retainedSamples, bool expected) {
        SoundBuffer sync, async;
        sync.samples.assign(retainedSamples, 0.25f);
        async.samples.assign(retainedSamples, 0.25f);
        sync.channels = async.channels = 1;
        sync.sampleRate = async.sampleRate = 48000;
        sync.numSamples = async.numSamples = retainedSamples;
        const bool syncResult = bool(sync.load(path));
        bool delivered = false, asyncResult = false;
        auto listener = async.onLoad.listen([&](LoadResult& result) { delivered = true; asyncResult = bool(result); });
        async.loadAsync(path);
        internal::waitForResourceLoadsForTests();
        internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
        check("populated replacement sync/async result parity", delivered && syncResult == expected && asyncResult == syncResult);
        check("async replacement publishes only complete PCM and metadata", expected ?
            (async.samples == sync.samples && async.numSamples == sync.numSamples && async.channels == sync.channels && async.sampleRate == sync.sampleRate) :
            (async.samples.size() == retainedSamples && async.samples.front() == 0.25f && async.numSamples == retainedSamples && async.channels == 1 && async.sampleRate == 48000));
    };
    run(900 * 1024 / sizeof(float), true);
    run(16 * 1024 / sizeof(float), true);
    internal::setAllocationLimitForTests(1);
    run(900 * 1024 / sizeof(float), false);
    internal::setAllocationLimitForTests(0);
    internal::setAudioWorkingLimitForTests(0);
    internal::appRunToken() = token;
}
#if defined(__linux__) && !defined(__ANDROID__)
// A backend with no frame hooks isolates the converter's format check.
void converterFormatHook() {
    ma_data_source_base source{};
    ma_data_source_vtable sourceVtable{};
    sourceVtable.onGetDataFormat = [](ma_data_source*, ma_format* format, ma_uint32* channels,
                                    ma_uint32* rate, ma_channel*, size_t) -> ma_result {
        if (format) *format = ma_format_f32;
        if (channels) *channels = 1;
        if (rate) *rate = 384001;
        return MA_SUCCESS;
    };
    auto sourceConfig = ma_data_source_config_init();
    sourceConfig.vtable = &sourceVtable;
    check("converter test source initializes", ma_data_source_init(&sourceConfig, &source) == MA_SUCCESS);
    ma_decoding_backend_vtable backend{};
    backend.onInitMemory = [](void* user, const void*, size_t, const ma_decoding_backend_config*,
                              const ma_allocation_callbacks*, ma_data_source** out) -> ma_result {
        *out = user; return MA_SUCCESS;
    };
    backend.onUninit = [](void*, ma_data_source*, const ma_allocation_callbacks*) {};
    ma_decoding_backend_vtable* backends[] = {&backend};
    internal::AudioDecodeBudget budget;
    auto config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    config.ppCustomBackendVTables = backends;
    config.customBackendCount = 1;
    config.pCustomBackendUserData = &source;
    config.allocationCallbacks = {&budget, internal::AudioDecodeBudget::allocate,
        internal::AudioDecodeBudget::reallocate, internal::AudioDecodeBudget::release};
    ma_decoder decoder;
    const char input = 0;
    const auto result = ma_decoder_init_memory(&input, 1, &config, &decoder);
    check("miniaudio converter rejects source format before allocating", result != MA_SUCCESS && budget.rejected && budget.peak == 0);
    if (result == MA_SUCCESS) ma_decoder_uninit(&decoder);
    ma_data_source_uninit(&source);
}

// Force FLAC so another backend cannot hide the metadata admission point.
void flacMetadataHook(const vector<char>& bytes) {
    internal::AudioDecodeBudget budget;
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    config.encodingFormat = ma_encoding_format_flac;
    config.allocationCallbacks = {&budget, internal::AudioDecodeBudget::allocate,
        internal::AudioDecodeBudget::reallocate, internal::AudioDecodeBudget::release};
    ma_decoder decoder;
    const auto result = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder);
    if (result == MA_SUCCESS) ma_decoder_uninit(&decoder);
    // The decoded-sample allocation alone needs 4096 * sizeof(int32_t).
    // The small backend wrapper may allocate before metadata inspection.
    check("FLAC metadata rejects before decoded-sample allocation",
          result != MA_SUCCESS && budget.rejected && budget.peak < 4096 * sizeof(int32_t) && budget.live == 0);
}

void vorbisCommentLengths(vector<char> ogg, const fs::path& dir) {
    const char signature[] = "\x03vorbis";
    const auto packet = std::search(ogg.begin(), ogg.end(), signature, signature + 7);
    check("Vorbis comment fixture has normal header", packet != ogg.end());
    if (packet == ogg.end()) return;
    const size_t vendorLengthOffset = size_t(packet - ogg.begin()) + 7;
    auto read32 = [&](size_t offset) {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n |= uint32_t(uint8_t(ogg[offset+i])) << (8*i);
        return n;
    };
    const size_t countOffset = vendorLengthOffset + 4 + read32(vendorLengthOffset);
    check("Vorbis fixture starts with no user comments", countOffset + 4 < ogg.size() && read32(countOffset) == 0);
    if (countOffset + 4 >= ogg.size() || read32(countOffset)) return;
    size_t page = 0;
    while (page + 27 < ogg.size()) {
        size_t length = 27 + uint8_t(ogg[page + 26]);
        for (size_t i = 0; i < uint8_t(ogg[page + 26]); ++i) length += uint8_t(ogg[page + 27 + i]);
        if (countOffset < page + length) break;
        page += length;
    }
    // The fixture's comment packet is the first, short segment on this page.
    check("Vorbis fixture comment fits one segment", page + 27 < ogg.size() && uint8_t(ogg[page+27]) < 243);
    if (page + 27 >= ogg.size() || uint8_t(ogg[page+27]) >= 243) return;
    ogg[countOffset] = 1;
    const vector<char> comment = {8, 0, 0, 0, 'T', 'I', 'T', 'L', 'E', '=', 'J', '2'};
    ogg.insert(ogg.begin() + countOffset + 4, comment.begin(), comment.end());
    ogg[page+27] = char(uint8_t(ogg[page+27]) + comment.size());
    auto checksum = [&](vector<char>& bytes) {
        size_t length = 27 + uint8_t(bytes[page+26]);
        for (size_t i = 0; i < uint8_t(bytes[page+26]); ++i) length += uint8_t(bytes[page+27+i]);
        for (int i = 0; i < 4; ++i) bytes[page+22+i] = 0;
        uint32_t crc = 0;
        for (size_t i = 0; i < length; ++i) {
            crc ^= uint32_t(uint8_t(bytes[page+i])) << 24;
            for (int bit = 0; bit < 8; ++bit) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04c11db7u : crc << 1;
        }
        for (int i = 0; i < 4; ++i) bytes[page+22+i] = char(crc >> (8*i));
    };
    checksum(ogg);
    const auto validPath = dir / "valid-comments.ogg";
    writeBytes(validPath, ogg);
    SoundBuffer valid;
    check("valid Vorbis with vendor and user comments loads from memory and file",
          bool(valid.loadOggFromMemory(ogg.data(), ogg.size())) && valid.numSamples == 10000 &&
          bool(valid.loadOgg(validPath)) && valid.numSamples == 10000);
    for (size_t offset : {vendorLengthOffset, countOffset + 4}) {
        auto truncated = ogg;
        for (int i = 0; i < 4; ++i) truncated[offset+i] = char((uint32_t(1) << 30) >> (8*i));
        checksum(truncated);
        const auto path = dir / "truncated-comment.ogg";
        writeBytes(path, truncated);
        for (bool file : {false, true}) {
            SoundBuffer previous;
            previous.samples = {0.25f}; previous.numSamples = 1; previous.channels = 1; previous.sampleRate = 48000;
            audioMallocProbeArm();
            const bool ok = bool(file ? previous.loadOgg(path) : previous.loadOggFromMemory(truncated.data(), truncated.size()));
            const size_t largest = audioMallocProbeDisarm();
            printf("Vorbis claimed comment: %s, %s, largest malloc=%zu\n",
                   offset == vendorLengthOffset ? "vendor" : "user", file ? "file" : "memory", largest);
            check("Vorbis truncated comment fails cleanly keeping PCM and metadata", !ok && intact(previous));
            check("Vorbis comment length cannot request large raw malloc", largest <= 1024 * 1024);
        }
    }
}

void longAac(const fs::path& dir) {
    // Generate a moov atom larger than 16 KiB; skip only unavailable tooling/plugins.
    if (std::system("command -v gst-launch-1.0 >/dev/null 2>&1 && gst-inspect-1.0 avenc_aac >/dev/null 2>&1 && gst-inspect-1.0 mp4mux >/dev/null 2>&1 && gst-inspect-1.0 audiotestsrc >/dev/null 2>&1") != 0) {
        printf("SKIP: long M4A requires GStreamer AAC encoder/muxer\n"); return;
    }
    const auto path = dir / "large-moov.m4a";
    const string command = "gst-launch-1.0 -q audiotestsrc num-buffers=10000 ! audioconvert ! avenc_aac ! mp4mux ! filesink location='" + path.string() + "'";
    check("long M4A fixture generated", std::system(command.c_str()) == 0);
    SoundBuffer file;
    constexpr size_t expectedFrames = 10000 * 1024 + 1024; // encoder delay included by this muxer
    check("large-moov M4A file loads all frames", bool(file.loadAac(path)) && file.numSamples == expectedFrames && file.channels == 1);
    ifstream input(path, ios::binary);
    vector<char> bytes((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    SoundBuffer memory;
    check("large-moov M4A memory loads all frames", bool(memory.loadAacFromMemory(bytes.data(), bytes.size())) &&
        memory.numSamples == expectedFrames && memory.channels == 1 && memory.samples == file.samples);
}
void frameAccounting() {
    using internal::AudioDecodeBudget;
    internal::setAudioWorkingLimitForTests(4096);
    {
        AudioDecodeBudget budget(0, true);
        internal::FFmpegAudio decoder(&budget);
        decoder.frame = av_frame_alloc();
        check("FFmpeg test frame allocation", decoder.frame != nullptr);
        if (decoder.frame) {
            decoder.frame->buf[0] = av_buffer_alloc(1024);
            check("FFmpeg packet-backed frame allocation", decoder.frame->buf[0] != nullptr);
            if (decoder.frame->buf[0]) {
                decoder.frame->buf[1] = av_buffer_ref(decoder.frame->buf[0]);
                check("FFmpeg shared buffer reference", decoder.frame->buf[1] != nullptr);
                check("packet-backed frame and resampler/output fit without double counting aliases",
                    decoder.prepareSamples(128) && budget.peak == 3072);
                check("packet-backed frame remains charged during output replacement",
                    !decoder.prepareSamples(160) && decoder.samples.capacity() == 256);
            }
        }
    }
    {
        AudioDecodeBudget budget(0, true);
        {
            internal::FFmpegAudio decoder(&budget);
            decoder.frame = av_frame_alloc();
            check("FFmpeg controlled frame allocation", decoder.frame != nullptr);
            if (decoder.frame) {
                decoder.frame->format = AV_SAMPLE_FMT_FLT;
                decoder.frame->sample_rate = 48000;
                decoder.frame->nb_samples = 128;
                av_channel_layout_default(&decoder.frame->ch_layout, 2);
                AVCodecContext context{};
                context.opaque = &decoder;
                const int result = internal::FFmpegAudio::getFrameBuffer(&context, decoder.frame, 0);
                check("FFmpeg controlled buffer is charged before allocation", result == 0 && budget.live == 1024 + decoder.framePrefixBytes);
                check("controlled frame is not charged twice during output allocation",
                    result == 0 && decoder.prepareSamples(128) && budget.peak == budget.live + 2048);
            }
        }
        check("FFmpeg controlled frame cleanup releases its budget", budget.live == 0);
    }
    internal::setAudioWorkingLimitForTests(0);
}
#endif

// Real device fixtures use the production thresholds, in a fresh process.
vector<char> readDeviceFile(const fs::path& path, size_t limit) {
    ifstream file(path, ios::binary);
    if (!file) throw runtime_error("cannot open device input");
    const auto size = min<uintmax_t>(fs::file_size(path), limit);
    vector<char> bytes(static_cast<size_t>(size));
    file.read(bytes.data(), static_cast<streamsize>(bytes.size()));
    if (file.bad() || file.fail() || file.gcount() != static_cast<streamsize>(bytes.size()))
        throw runtime_error("cannot read device input completely");
    return bytes;
}

void deviceFixtureChecks(const fs::path& inputPath, const string& expect) {
    const auto path = fs::absolute(inputPath);
    const bool reject = expect == "reject-encoded" || expect == "reject-format";
    const bool notice = expect == "notice";
    check("known expectation", reject || notice || expect == "load");
    size_t notices = 0, warnings = 0;
    string rejectionMessage;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Notice && event.message.find("decoded audio exceeds") != string::npos) {
            ++notices;
            check("Notice recommends streaming", event.message.find("SoundStream::loadStream()") != string::npos);
        }
        if (event.level == LogLevel::Warning && event.message.find("rejecting whole sound") != string::npos) {
            ++warnings;
            rejectionMessage = event.message;
        }
    });
    // Bound (test): retain at most 64 MiB + 1 encoded bytes for rejection probes, and 1 MiB of manifest JSON.
    const auto bytes = readDeviceFile(path, 64 * 1024 * 1024 + 1);
    check("fixture is nonempty", !bytes.empty());
    const auto manifestBytes = readDeviceFile(path.parent_path() / "manifest.json", 1024 * 1024);
    const auto manifest = nlohmann::json::parse(manifestBytes.begin(), manifestBytes.end());
    // Read only the scalar expectations by reference: copying a whole manifest
    // row would recurse through any nested value it carries.
    const nlohmann::json* expected = nullptr;
    for (const auto& entry : manifest) {
        if (entry.is_object() && entry.contains("file") && entry["file"].is_string() &&
            entry["file"].get<string>() == path.filename().string()) {
            if (expected) throw runtime_error("duplicate fixture in manifest");
            expected = &entry;
        }
    }
    if (!expected) throw runtime_error("fixture missing from manifest");
    check("manifest expectation matches command", expected->at("expect").get<string>() == expect);
    const nlohmann::json* pcm = expected;
#if defined(__linux__) && !defined(__ANDROID__)
    if (expected->contains("linux_pcm")) pcm = &expected->at("linux_pcm");
#endif
    const auto channels = expected->at("channels").get<int>();
    const auto rate = pcm->at("sample_rate").get<int>();
    const auto frames = pcm->at("frames").get<int64_t>();
    const auto tolerance = expected->at("frame_tolerance").get<int64_t>();
    if (channels <= 0 || rate <= 0 || frames <= 0 || tolerance < 0 || tolerance >= frames)
        throw runtime_error("invalid manifest PCM expectation");
    auto checkPcm = [&](const SoundBuffer& b) {
        check("PCM channels match manifest", b.channels == channels);
        check("PCM sample rate matches manifest", b.sampleRate == rate);
        check("PCM frames match full manifest duration", abs(double(b.numSamples) - double(frames)) <= double(tolerance));
        check("complete PCM layout", b.channels > 0 && b.numSamples <= b.samples.size() / size_t(b.channels) &&
              b.samples.size() == b.numSamples * size_t(b.channels));
        check("decoded frame count replaced seed", b.numSamples != 1);
        check("decoded PCM content replaced seed", b.samples != vector<float>{0.25f});
    };
    auto checkDuration = [&](const Sound& sound) {
        // Sound exposes float seconds; allow its rounding in addition to codec padding.
        const double duration = double(frames) / rate;
        const double secondsTolerance = double(tolerance) / rate +
            2 * numeric_limits<float>::epsilon() * max(1.0, duration);
        check("Sound duration matches manifest", abs(double(sound.getDuration()) - duration) <= secondsTolerance);
    };
    string ext = path.extension().string();
    transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(tolower(c)); });
    auto memoryLoad = [&](SoundBuffer& b) -> LoadResult {
        if (ext == ".wav") return b.loadWavFromMemory(bytes.data(), bytes.size());
        if (ext == ".mp3") return b.loadMp3FromMemory(bytes.data(), bytes.size());
        if (ext == ".flac") return b.loadFlacFromMemory(bytes.data(), bytes.size());
        if (ext == ".ogg") return b.loadOggFromMemory(bytes.data(), bytes.size());
        return b.loadAacFromMemory(bytes.data(), bytes.size());
    };
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    auto& engine = AudioEngine::getInstance();
    check("null backend starts (decode only)", engine.init(settings));
    auto run = [&](const char* label, auto load) {
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto before = internal::audioDecodeRejections();
            SoundBuffer b;
            b.samples = {0.25f}; b.channels = 1; b.sampleRate = 48000; b.numSamples = 1;
            const bool ok = bool(load(b));
            check(label, ok == !reject);
            check("whole-load rejection count", internal::audioDecodeRejections() == before + (reject ? 1 : 0));
            if (reject) check("failed load keeps previous PCM", intact(b));
            else {
                checkPcm(b);
                if (notice) check("actual PCM exceeds 256 MiB", b.samples.size() > internal::audioLargeLoadNoticeBytes / sizeof(float));
            }
        }
    };
    if (!failures) {
        run("SoundBuffer file load", [&](SoundBuffer& b) { return b.load(path); });
        run("SoundBuffer memory load", memoryLoad);
        // Sound has no encoded-memory API. Attach the memory-decoded buffer
        // through its public loadFromBuffer entry instead.
        run("Sound memory buffer attachment", [&](SoundBuffer& b) {
            auto result = memoryLoad(b);
            if (result) {
                auto decoded = make_shared<SoundBuffer>(std::move(b));
                {
                    Sound sound;
                    sound.loadFromBuffer(decoded);
                    check("Sound memory attachment loaded state", sound.isLoaded());
                    checkDuration(sound);
                }
                b = std::move(*decoded);
            }
            return result;
        });
        for (int repeat = 0; repeat < 2; ++repeat) {
            Sound sound;
            const auto before = internal::audioDecodeRejections();
            check("Sound file load", bool(sound.load(path)) == !reject);
            check("Sound loaded state", sound.isLoaded() == !reject);
            if (!reject) checkDuration(sound);
            check("Sound rejection count", internal::audioDecodeRejections() == before + (reject ? 1 : 0));
        }
#if defined(__APPLE__) || defined(_WIN32)
        // Explicit native MP3 coverage: automatic .mp3 dispatch is miniaudio.
        if (ext == ".mp3") {
            run("native MP3 file load", [&](SoundBuffer& b) { return b.loadAac(path); });
            run("native MP3 memory load", [&](SoundBuffer& b) { return b.loadAacFromMemory(bytes.data(), bytes.size()); });
        }
#endif
        check("one Notice across repeated loads", notices == (notice ? 1u : 0u));
        check("one first rejection Warning", warnings == (reject ? 1u : 0u));
        if (expect == "reject-encoded") check("encoded rejection reason", rejectionMessage.find("encoded audio bytes") != string::npos);
        if (expect == "reject-format") check("format rejection reason", rejectionMessage.find("audio channels") != string::npos || rejectionMessage.find("audio sample rate") != string::npos);
    }
}

int deviceFixture(const fs::path& inputPath, const string& expect) {
    try {
        deviceFixtureChecks(inputPath, expect);
    } catch (const bad_alloc&) {
        check("device fixture allocation failed", false);
    } catch (const exception& error) {
        printf("Device fixture error: %s\n", error.what());
        check("device fixture input or decode failed", false);
    }
    AudioEngine::getInstance().shutdown();
    printf("Audio device fixture: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    fflush(stdout); // iOS launchers consume the verdict even when main's status is unavailable.
    return failures ? 1 : 0;
}
}

TC_CORE_TEST_MAIN(int argc, char** argv) {
    using namespace tc;
    using namespace std;
    using internal::AudioDecodeBudget;
    getMainThreadId();
    if (argc != 1) {
        if (argc == 4 && string(argv[1]) == "--device-fixture")
            return deviceFixture(argv[2], argv[3]);
        check("usage: --device-fixture <path> <load|reject-encoded|reject-format|notice>", false);
        printf("Audio device fixture: FAIL (%d failures)\n", failures);
        return 1;
    }
    const auto dir = fs::temp_directory_path() / "tc_audioDecodeBounds";
    fs::create_directories(dir);
    size_t warnings = 0, streamWarnings = 0, notices = 0, allocationErrors = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Notice && event.message.find("decoded audio exceeds") != string::npos) {
            ++notices;
            check("large decode Notice names streaming API", event.message.find("SoundStream::loadStream()") != string::npos);
        }
        if (event.level == LogLevel::Error && event.message.find("audio allocation failed") != string::npos) ++allocationErrors;
        if (event.level == LogLevel::Warning && event.message.find("rejecting whole stream") != string::npos) ++streamWarnings;
        if (event.level == LogLevel::Warning && event.message.find("rejecting whole sound") != string::npos) {
            ++warnings;
            check("full-buffer rejection names streaming API", event.message.find("SoundStream::loadStream()") != string::npos);
        }
    });
    const auto before = internal::audioDecodeRejections();
    {
        AudioDecodeBudget b;
        check("encoded limit accepts exact boundary", b.encoded(internal::audioEncodedLimit));
        check("encoded overflow rejects whole unit", !b.encoded(internal::audioEncodedLimit + 1));
        check("a failed budget counts only once", !b.encoded(internal::audioEncodedLimit + 2) && internal::audioDecodeRejections() == before + 1);
    }
    {
        AudioDecodeBudget b;
        check("format endpoints accepted", b.format(32, 384000));
        check("invalid rate rejected", !b.format(1, numeric_limits<double>::quiet_NaN()));
    }
    {
        internal::setAudioWorkingLimitForTests(4096);
        AudioDecodeBudget b(0, true);
        void* p = AudioDecodeBudget::allocate(4096 - sizeof(AudioDecodeBudget::Allocation), &b);
        check("allocator exact live boundary is complete", p && b.live == 4096 && b.peak == 4096);
        check("replacement includes original live allocation", !AudioDecodeBudget::reallocate(p, 1, &b));
        AudioDecodeBudget::release(p, nullptr);
        check("failed replacement releases original cleanly", b.live == 0);
        internal::setAudioWorkingLimitForTests(0);
    }
    {
        internal::setAudioWorkingLimitForTests(4096);
        AudioDecodeBudget b(0, true);
        void* vendor = AudioDecodeBudget::allocate(1024 - sizeof(AudioDecodeBudget::Allocation), &b);
        vector<float> pcm;
        check("decoder, PCM and scratch fit together at exact boundary", vendor && internal::growAudioVector(pcm, 512, b, 1024));
        check("replacement copy is charged alongside scratch and decoder", !internal::growAudioVector(pcm, 768, b, 1024) && pcm.capacity() == 512);
        check("observed live peak respects working budget", b.peak == 4096);
        AudioDecodeBudget::release(vendor, nullptr);
        internal::setAudioWorkingLimitForTests(0);
    }
#if defined(__linux__) && !defined(__ANDROID__)
    frameAccounting();
    converterFormatHook();
#endif
    // Lower only the Notice threshold, then exercise real decoding/append.
    internal::setAudioNoticeThresholdForTests(16384);
    const auto longDecode = flacBytes(16, 0);
    SoundBuffer longBuffer;
    const auto beforeNotice = internal::audioDecodeRejections();
    check("decode past Notice threshold succeeds completely", bool(longBuffer.loadFlacFromMemory(longDecode.data(), longDecode.size())) && longBuffer.numSamples == 16 * 4096);
    check("repeated long decode succeeds with one process-lifetime Notice", bool(longBuffer.loadFlacFromMemory(longDecode.data(), longDecode.size())) && notices == 1);
    check("large decode Notice is not a loss", internal::audioDecodeRejections() == beforeNotice);
    internal::setAudioNoticeThresholdForTests(0);
    const auto keepPcm = longBuffer.samples;
    const auto keepFrames = longBuffer.numSamples;
    const auto allocationBefore = allocationErrors;
    internal::setAllocationLimitForTests(1);
    check("allocation failure returns failure and keeps previous PCM", !longBuffer.loadFlacFromMemory(longDecode.data(), longDecode.size()) && longBuffer.samples == keepPcm && longBuffer.numSamples == keepFrames && longBuffer.channels == 1 && longBuffer.sampleRate == 48000);
    internal::setAllocationLimitForTests(0);
    check("allocation failure is an Error without input-loss counter", allocationErrors == allocationBefore + 1 && internal::audioDecodeRejections() == beforeNotice);
    const auto replacementPath = dir / "replacement.wav";
    writeBytes(replacementPath, wavBytes(51200));
    asyncReplacement(replacementPath);
    const auto smallWav = wavBytes(1);
    SoundBuffer buffer;
    check("valid WAV accepted", bool(buffer.loadWavFromMemory(smallWav.data(), smallWav.size())) && intact(buffer));
    const auto formatBoundary = wavBytes(1, 32, 384000);
    SoundBuffer wideBuffer;
    check("valid format boundary remains complete", bool(wideBuffer.loadWavFromMemory(formatBoundary.data(), formatBoundary.size())) &&
          wideBuffer.channels == 32 && wideBuffer.sampleRate == 384000 && wideBuffer.numSamples == 1 && wideBuffer.samples.size() == 32);
    const char marker = 0;
    const size_t overInput = internal::audioEncodedLimit + 1;
    check("WAV encoded admission precedes decode", !buffer.loadWavFromMemory(&marker, overInput) && intact(buffer));
    check("MP3 encoded admission precedes decode", !buffer.loadMp3FromMemory(&marker, overInput) && intact(buffer));
    check("FLAC encoded admission precedes decode", !buffer.loadFlacFromMemory(&marker, overInput) && intact(buffer));
    check("Vorbis encoded admission precedes decode", !buffer.loadOggFromMemory(&marker, overInput) && intact(buffer));
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    check("AAC encoded admission precedes decode", !buffer.loadAacFromMemory(&marker, overInput) && intact(buffer));
#endif
    for (auto bytes : {wavBytes(1, 33), wavBytes(1, 1, 384001)})
        check("WAV format rejected without publishing", !buffer.loadWavFromMemory(bytes.data(), bytes.size()) && intact(buffer));
    const auto sparse = dir / "input.wav";
    writeBytes(sparse, smallWav);
    fs::resize_file(sparse, overInput);
    check("file admission precedes read/decode", !buffer.loadWav(sparse) && intact(buffer));
    const auto largeStatement = flacBytes(2, uint64_t(1) << 27);
    check("huge stated length loads actual FLAC frames", bool(buffer.loadFlacFromMemory(largeStatement.data(), largeStatement.size())) && buffer.numSamples == 2 * 4096);
    auto invalidFlac = flacBytes(1, 4096);
    const uint64_t invalidFlacFormat = (uint64_t(384001) << 44) | (uint64_t(15) << 36) | 4096;
    for (int i = 0; i < 8; ++i) invalidFlac[18 + i] = char(invalidFlacFormat >> (56 - 8*i));
    check("FLAC source format is rejected before size-driven setup", !buffer.loadFlacFromMemory(invalidFlac.data(), invalidFlac.size()) && buffer.numSamples == 2 * 4096);
#if defined(__linux__) && !defined(__ANDROID__)
    flacMetadataHook(invalidFlac);
#endif
    const auto changedFlac = flacBytes(2, 0, true);
    const auto beforeFlacChange = internal::audioDecodeRejections();
    check("FLAC frame rate change rejects through frame hook and keeps previous PCM",
          !buffer.loadFlacFromMemory(changedFlac.data(), changedFlac.size()) && buffer.numSamples == 2 * 4096 &&
          buffer.channels == 1 && buffer.sampleRate == 48000 &&
          internal::audioDecodeRejections() == beforeFlacChange + 1);
    const auto flac = flacBytes(16, 0); // unspecified duration, checked by running total
    internal::setAudioWorkingLimitForTests(256 * 1024);
    const auto oneBlockFlac = flacBytes(1, 0);
    SoundBuffer oneBlock;
    check("FLAC setup and first output fit the working budget", bool(oneBlock.loadFlacFromMemory(oneBlockFlac.data(), oneBlockFlac.size())) && oneBlock.numSamples == 4096);
    check("full-load expansion ignores streaming budget", bool(buffer.loadFlacFromMemory(flac.data(), flac.size())) && buffer.numSamples == 16 * 4096);
    internal::setAudioWorkingLimitForTests(0);
    check("valid FLAC remains complete", bool(buffer.loadFlacFromMemory(flac.data(), flac.size())) && buffer.numSamples == 16 * 4096);
    const auto flacPath = dir / "replacement.flac";
    writeBytes(flacPath, flac);
    asyncReplacement(flacPath);
    vector<char> mp3;
    for (int i = 0; i < 16; ++i) {
        char frame[96] = {};
        frame[0] = char(0xFF); frame[1] = char(0xFB); frame[2] = char(0x14);
        mp3.insert(mp3.end(), frame, frame + sizeof(frame));
    }
    SoundBuffer mp3Buffer;
    check("valid MP3 remains complete", bool(mp3Buffer.loadMp3FromMemory(mp3.data(), mp3.size())) && mp3Buffer.numSamples == 16 * 1152);
    internal::setAudioWorkingLimitForTests(16384);
    SoundBuffer limitedMp3;
    check("MP3 full load ignores streaming budget", bool(limitedMp3.loadMp3FromMemory(mp3.data(), mp3.size())) && limitedMp3.samples == mp3Buffer.samples);
    internal::setAudioWorkingLimitForTests(0);
    const auto mp3Path = dir / "replacement.mp3";
    writeBytes(mp3Path, mp3);
    asyncReplacement(mp3Path);
    auto changedMp3 = mp3;
    for (size_t i = 8 * 96 + 3; i < changedMp3.size(); i += 96) changedMp3[i] = char(0xC0);
    SoundBuffer changed;
    check("changed MP3 layout rejects whole full-buffer unit", !changed.loadMp3FromMemory(changedMp3.data(), changedMp3.size()) && changed.samples.empty());
    const auto ogg = vorbisToneBytes();
    auto wideVorbis = ogg;
    wideVorbis[39] = 33;
    SoundBuffer rejectedVorbis;
    const auto beforeVorbisFormat = internal::audioDecodeRejections();
    check("Vorbis source format is checked before setup", !rejectedVorbis.loadOggFromMemory(wideVorbis.data(), wideVorbis.size()) &&
          internal::audioDecodeRejections() == beforeVorbisFormat + 1 && rejectedVorbis.samples.empty());
#if defined(__linux__) && !defined(__ANDROID__)
    vorbisCommentLengths(ogg, dir);
#endif
    internal::setAudioWorkingLimitForTests(16384);
    SoundBuffer vorbis;
    check("Vorbis full load ignores streaming budget", bool(vorbis.loadOggFromMemory(ogg.data(), ogg.size())) && vorbis.numSamples == 10000);
    internal::setAudioWorkingLimitForTests(0);
    check("valid Vorbis remains complete", bool(vorbis.loadOggFromMemory(ogg.data(), ogg.size())) && vorbis.numSamples == 10000);
    const auto oggPath = dir / "replacement.ogg";
    writeBytes(oggPath, ogg);
    asyncReplacement(oggPath);
    short* rawPcm = nullptr;
    int rawChannels = 0, rawRate = 0;
    const int rawFrames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(ogg.data()), int(ogg.size()), &rawChannels, &rawRate, &rawPcm);
    check("direct Vorbis PCM keeps its malloc/free contract", rawFrames == 10000 && rawPcm && rawChannels == 2 && rawRate == 8000);
    std::free(rawPcm);
#if defined(__linux__) && !defined(__ANDROID__)
    longAac(dir);
    SoundBuffer aac;
    check("GStreamer memory route remains supported", bool(aac.loadAacFromMemory(toneAac, sizeof(toneAac))) && !aac.samples.empty());
    const auto aacPath = dir / "replacement.aac";
    writeBytes(aacPath, vector<char>(reinterpret_cast<const char*>(toneAac), reinterpret_cast<const char*>(toneAac) + sizeof(toneAac)));
    asyncReplacement(aacPath);
    internal::setAudioWorkingLimitForTests(140000);
    SoundBuffer limitedAac;
    check("GStreamer full load ignores streaming budget", bool(limitedAac.loadAacFromMemory(toneAac, sizeof(toneAac))) && limitedAac.samples == aac.samples);
    internal::setAudioWorkingLimitForTests(0);
#endif
    const auto invalidStreamPath = dir / "format.wav";
    writeBytes(invalidStreamPath, wavBytes(1, 33));
    const auto streamRejectionsBefore = internal::audioDecodeRejections(true);
    SoundStream invalidStream;
    check("stream rejects source format before conversion", !invalidStream.loadStream(invalidStreamPath));
    check("repeated stream rejection still rejects whole unit", !invalidStream.loadStream(invalidStreamPath));
    check("stream rejection counter and first Warning are exact", internal::audioDecodeRejections(true) == streamRejectionsBefore + 2 && streamWarnings == 1);
    const auto streamPath = dir / "duration.flac";
    writeBytes(streamPath, largeStatement);
    SoundStream stream;
    check("stream duration is not capped by full PCM limit", bool(stream.loadStream(streamPath)) && stream.getDuration() > 1000);
    const auto longPath = dir / "long.wav";
    auto longHeader = wavBytes(1);
    const uint32_t dataBytes = 300u * 1024 * 1024;
    auto set32 = [&](size_t offset, uint32_t value) {
        for (int i = 0; i < 4; ++i) longHeader[offset + i] = char(value >> (8*i));
    };
    set32(4, 36 + dataBytes); set32(40, dataBytes);
    writeBytes(longPath, longHeader);
    fs::resize_file(longPath, 44 + dataBytes);
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    auto& engine = AudioEngine::getInstance();
    check("null audio backend starts", engine.init(settings));
    Sound longSound;
    check("long file streams despite full-buffer encoded-input limit", bool(longSound.loadStream(longPath)) && longSound.play());
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while (longSound.getPosition() <= 0 && chrono::steady_clock::now() < deadline)
        this_thread::sleep_for(chrono::milliseconds(2));
    check("long stream decodes and advances with its existing ring", longSound.getPosition() > 0 && longSound.isPlaying());
    longSound.stop();
    engine.shutdown();
    check("one first-loss warning across repeated full loads", warnings == 1);
    check("one process-lifetime large-load Notice across all formats and async loads", notices == 1);
    uint64_t expectedRejections = 13; // encoded/format/arithmetic bounds only
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    ++expectedRejections;
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    expectedRejections += 2; // converter format and isolated FLAC metadata
#endif
    const auto rejectionCount = internal::audioDecodeRejections();
    check("whole-load rejection total is exact and queries do not reset it", rejectionCount == before + expectedRejections && internal::audioDecodeRejections() == rejectionCount);
    fs::remove_all(dir);
    return failures ? 1 : 0;
}
