#include <TrussC.h>
#include "tc/sound/internal/tcAudioDecode.h"
#include "../../common/tcCoreTest.h"
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
vector<char> wavBytes(unsigned frames, unsigned channels = 1, unsigned rate = 48000, unsigned bits = 16) {
    vector<char> bytes;
    auto text = [&](const char* p, size_t n) { bytes.insert(bytes.end(), p, p + n); };
    auto u16 = [&](unsigned v) { bytes.push_back(char(v)); bytes.push_back(char(v >> 8)); };
    auto u32 = [&](unsigned v) { for (int i = 0; i < 4; ++i) bytes.push_back(char(v >> (8*i))); };
    const unsigned frameBytes = channels * (bits / 8);
    text("RIFF", 4); u32(36 + frames * frameBytes); text("WAVEfmt ", 8);
    u32(16); u16(1); u16(channels); u32(rate); u32(rate * frameBytes);
    u16(frameBytes); u16(bits); text("data", 4); u32(frames * frameBytes);
    for (unsigned i = 0; i < frames * channels; ++i) {
        if (bits == 8) bytes.push_back(char(160));
        else u16(8192);
    }
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
    internal::appRunToken() = token;
}
#if defined(__linux__) && !defined(__ANDROID__)
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

void changingAac(const fs::path& dir) {
    if (std::system("command -v gst-launch-1.0 >/dev/null 2>&1 && gst-inspect-1.0 avenc_aac >/dev/null 2>&1 && gst-inspect-1.0 aacparse >/dev/null 2>&1") != 0) {
        printf("SKIP: changing AAC requires GStreamer AAC encoder/parser\n"); return;
    }
    vector<char> joined;
    size_t expectedFrames = 0;
    for (int part = 0; part < 2; ++part) {
        const auto path = dir / ("part-" + to_string(part) + ".aac");
        const string command = "gst-launch-1.0 -q audiotestsrc num-buffers=32 ! audio/x-raw,channels=" +
            to_string(part + 1) + ",rate=" + to_string(part ? 48000 : 32000) +
            " ! audioconvert ! avenc_aac ! aacparse ! audio/mpeg,stream-format=adts ! filesink location='" + path.string() + "'";
        check("AAC segment generated offline", std::system(command.c_str()) == 0);
        ifstream input(path, ios::binary);
        vector<char> bytes((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
        check("AAC segment is nonempty", !bytes.empty());
        SoundBuffer segment;
        check("AAC segment loads independently", bool(segment.loadAac(path)));
        expectedFrames += segment.numSamples;
        joined.insert(joined.end(), bytes.begin(), bytes.end());
    }
    const auto path = dir / "changing.aac";
    writeBytes(path, joined);
    SoundBuffer file, memory;
    const auto before = internal::audioDecodeRejections();
    check("GStreamer loads AAC with changing input format", bool(file.loadAac(path)));
    check("GStreamer loads changing AAC from memory", bool(memory.loadAacFromMemory(joined.data(), joined.size())));
    // Allow resampler rounding/tail at the transition between complete segments.
    check("changing AAC produces complete matching PCM", file.sampleRate == 44100 &&
          file.channels == 1 && expectedFrames > 0 &&
          abs(double(file.numSamples) - double(expectedFrames)) <= 64 &&
          file.samples.size() == file.numSamples * size_t(file.channels) && memory.samples == file.samples &&
          memory.channels == file.channels && memory.sampleRate == file.sampleRate && memory.numSamples == file.numSamples);
    check("changing AAC is not counted as a rejection", internal::audioDecodeRejections() == before);
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
    const bool memoryRejected = expect == "load-file";
    const bool notice = expect == "notice";
    check("known expectation", memoryRejected || notice || expect == "load");
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
    auto run = [&](const char* label, bool reject, auto load) {
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
        run("SoundBuffer file load", false, [&](SoundBuffer& b) { return b.load(path); });
        run("SoundBuffer memory load", memoryRejected, memoryLoad);
        // Sound has no encoded-memory API. Attach the memory-decoded buffer
        // through its public loadFromBuffer entry instead.
        run("Sound memory buffer attachment", memoryRejected, [&](SoundBuffer& b) {
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
            check("Sound file load", bool(sound.load(path)));
            check("Sound loaded state", sound.isLoaded());
            checkDuration(sound);
            check("Sound rejection count", internal::audioDecodeRejections() == before);
        }
#if defined(__APPLE__) || defined(_WIN32)
        // Explicit native MP3 coverage: automatic .mp3 dispatch is miniaudio.
        if (ext == ".mp3") {
            run("native MP3 file load", false, [&](SoundBuffer& b) { return b.loadAac(path); });
            run("native MP3 memory load", memoryRejected, [&](SoundBuffer& b) { return b.loadAacFromMemory(bytes.data(), bytes.size()); });
        }
#endif
        check("one Notice across repeated loads", notices == (notice ? 1u : 0u));
        check("one first rejection Warning", warnings == (memoryRejected ? 1u : 0u));
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
    getMainThreadId();
    if (argc == 4 && string(argv[1]) == "--device-fixture") return deviceFixture(argv[2], argv[3]);
    if (argc != 1) return 1;
    const auto dir = fs::temp_directory_path() / ("tc_audioDecodeBounds_" +
        to_string(chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    struct RemoveFiles { fs::path path; ~RemoveFiles() { fs::remove_all(path); } } removeFiles{dir};
    struct WavFormat { unsigned channels, rate; };
    for (const auto& format : {WavFormat{254, 48000}, WavFormat{1, 384001},
                               WavFormat{1, 7}, WavFormat{1, unsigned(INT_MAX)}}) {
        printf("WAV fixture: %u channels, %u Hz\n", format.channels, format.rate);
        const auto bytes = wavBytes(3, format.channels, format.rate);
        SoundBuffer buffer;
        check("unusual WAV format loads from memory", bool(buffer.loadWavFromMemory(bytes.data(), bytes.size())) &&
              buffer.channels == int(format.channels) && buffer.sampleRate == int(format.rate) &&
              buffer.numSamples == 3 && buffer.samples.size() == size_t(format.channels) * 3);
        const auto path = dir / "unusual.wav";
        writeBytes(path, bytes);
        const auto pcm = buffer.samples;
        check("unusual WAV format loads from file", bool(buffer.loadWav(path)) &&
              buffer.channels == int(format.channels) && buffer.sampleRate == int(format.rate) &&
              buffer.numSamples == 3 && buffer.samples.size() == size_t(format.channels) * 3 && buffer.samples == pcm);
    }
    {
        // Mono 8-bit PCM keeps INT_MAX+1 representable in both WAV rate fields.
        const auto bytes = wavBytes(4096, 1, unsigned(INT_MAX) + 1u, 8);
        const auto path = dir / "unrepresentable-rate.wav";
        writeBytes(path, bytes);
        SoundBuffer buffer;
        const auto seed = wavBytes(1);
        check("rate failure starts with existing PCM", bool(buffer.loadWavFromMemory(seed.data(), seed.size())));
        size_t errors = 0;
        string message;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Error) { ++errors; message = event.message; }
        });
        const string reason = "sample rate 2147483648 Hz does not fit SoundBuffer's int sample rate";
        const auto memoryResult = buffer.loadWavFromMemory(bytes.data(), bytes.size());
        check("INT_MAX+1 memory rate fails cleanly with one representation Error", !memoryResult &&
              memoryResult.error == LoadError::DecodeFailed && intact(buffer) && errors == 1 &&
              message.find("memory input") != string::npos && message.find(reason) != string::npos);
        errors = 0;
        const auto fileResult = buffer.loadWav(path);
        check("INT_MAX+1 file rate fails cleanly with one named representation Error", !fileResult &&
              fileResult.error == LoadError::DecodeFailed && intact(buffer) && errors == 1 &&
              message.find("unrepresentable-rate.wav") != string::npos && message.find(reason) != string::npos);
    }
    // A real, complete local WAV past the old encoded-input cap. Sparse zero
    // PCM keeps fixture creation cheap; the decoder must still read every frame.
    const auto largePath = dir / "large.wav";
    auto header = wavBytes(1);
    const uint32_t dataBytes = 64u * 1024 * 1024 + 2;
    auto set32 = [&](size_t offset, uint32_t value) {
        for (int i = 0; i < 4; ++i) header[offset+i] = char(value >> (8*i));
    };
    set32(4, 36 + dataBytes); set32(40, dataBytes);
    writeBytes(largePath, header);
    fs::resize_file(largePath, 44 + dataBytes);
    {
        SoundBuffer large;
        check("local WAV above 64 MiB loads completely", bool(large.loadWav(largePath)) &&
              large.numSamples == dataBytes / 2 && large.channels == 1 && large.samples.size() == dataBytes / 2 &&
              large.samples.front() == 0.25f && large.samples.back() == 0);
    }
    const auto replacementPath = dir / "replacement.wav";
    writeBytes(replacementPath, wavBytes(51200));
    asyncReplacement(replacementPath);
    size_t notices = 0;
    auto noticeListener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Notice && event.message.find("decoded audio exceeds") != string::npos) ++notices;
    });
    internal::setAudioNoticeThresholdForTests(16384);
    const auto flac = flacBytes(16, uint64_t(1) << 27);
    SoundBuffer buffer;
    check("overstated FLAC loads actual decoded frames", bool(buffer.loadFlacFromMemory(flac.data(), flac.size())) &&
          buffer.numSamples == 16 * 4096);
    check("repeated decode retains the single informational Notice",
          bool(buffer.loadFlacFromMemory(flac.data(), flac.size())) && notices == 1);
    internal::setAudioNoticeThresholdForTests(0);
    const auto flacPath = dir / "replacement.flac";
    writeBytes(flacPath, flac);
    asyncReplacement(flacPath);
    const auto ogg = vorbisToneBytes();
#if defined(__linux__) && !defined(__ANDROID__)
    vorbisCommentLengths(ogg, dir);
#endif
    SoundBuffer vorbis;
    check("Vorbis loads completely", bool(vorbis.loadOggFromMemory(ogg.data(), ogg.size())) && vorbis.numSamples == 10000);
    const auto oggPath = dir / "replacement.ogg";
    writeBytes(oggPath, ogg);
    asyncReplacement(oggPath);
    short* rawPcm = nullptr;
    int rawChannels = 0, rawRate = 0;
    const int rawFrames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(ogg.data()), int(ogg.size()), &rawChannels, &rawRate, &rawPcm);
    check("direct Vorbis PCM keeps malloc/free contract", rawFrames == 10000 && rawPcm && rawChannels == 2 && rawRate == 8000);
    std::free(rawPcm);
#if defined(__linux__) && !defined(__ANDROID__)
    changingAac(dir);
    longAac(dir);
    SoundBuffer aac;
    check("GStreamer memory route remains supported", bool(aac.loadAacFromMemory(toneAac, sizeof(toneAac))) && !aac.samples.empty());
    const auto aacPath = dir / fs::path(u8"native-日本語.aac");
    const vector<char> aacBytes(reinterpret_cast<const char*>(toneAac),
                                reinterpret_cast<const char*>(toneAac) + sizeof(toneAac));
    writeBytes(aacPath, aacBytes);
    check("Linux AAC preserves native filesystem paths", bool(aac.loadAac(aacPath)));
    asyncReplacement(aacPath);
    // Filesystems with UTF-8-only names cannot create this optional fixture.
    const auto rawPath = dir / fs::path(string("native-") + char(0xff) + ".aac");
    ofstream rawFile(rawPath, ios::binary);
    if (rawFile) {
        rawFile.write(aacBytes.data(), streamsize(aacBytes.size()));
        rawFile.close();
        check("Linux AAC preserves non-UTF-8 filesystem bytes", bool(aac.loadAac(rawPath)));
    } else {
        printf("SKIP: filesystem cannot create a non-UTF-8 AAC filename\n");
    }

#endif
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    auto& engine = AudioEngine::getInstance();
    check("null audio backend starts", engine.init(settings));
    const auto widePath = dir / "wide.wav";
    writeBytes(widePath, wavBytes(4800, 254));
    Sound stream;
    check("254-channel WAV can stream and play", bool(stream.loadStream(widePath)) && stream.play());
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while (stream.isPlaying() && stream.getPosition() <= 0 && chrono::steady_clock::now() < deadline)
        this_thread::sleep_for(chrono::milliseconds(2));
    check("wide stream decoded frames", stream.getPosition() > 0);
    stream.stop();
    engine.shutdown();
    return failures ? 1 : 0;
}
