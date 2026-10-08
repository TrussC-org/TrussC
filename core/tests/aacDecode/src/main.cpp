#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>

namespace {
using namespace tc;

#include "toneAac.h"

int failures = 0;

void check(const char* label, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    std::fflush(stdout);
    if (!ok) ++failures;
}

bool writeFile(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();
    return !file.fail();
}

void checkFailure(const fs::path& path, const std::vector<unsigned char>& bytes) {
    check("write invalid M4A", writeFile(path, bytes));
    std::vector<std::string> errors;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error) errors.push_back(e.message);
    });
    SoundBuffer buffer;
    const auto result = buffer.load(path);
    check("invalid M4A returns DecodeFailed", !result && result.error == LoadError::DecodeFailed);
    check("invalid M4A logs exactly one error", errors.size() == 1);
    const std::string prefix = "GStreamer error for " + path.string() + ": ";
    const auto messageStart = errors.size() == 1 ? errors[0].find(prefix) : std::string::npos;
    check("error names the file and includes the GStreamer message",
          messageStart != std::string::npos && errors[0].size() > messageStart + prefix.size());
    check("failed decode does not publish samples", buffer.samples.empty());
}
} // namespace
#endif

TC_CORE_TEST_MAIN() {
#if defined(__linux__) && !defined(__ANDROID__)
    // No elapsed-time assertions: run this process under an external timeout.
    const fs::path dir = fs::temp_directory_path() / ("tc_aacDecode_" + std::to_string(getpid()));
    fs::create_directories(dir);
    const std::vector<unsigned char> valid(std::begin(toneAac), std::end(toneAac));

    // Preserve the M4A header, but remove most of mdat and the trailing moov.
    checkFailure(dir / "truncated.m4a", {valid.begin(), valid.begin() + 128});

    std::vector<unsigned char> garbage(4096);
    uint32_t state = 453;
    for (auto& byte : garbage) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<unsigned char>(state >> 24);
    }
    checkFailure(dir / "garbage.m4a", garbage);

    const fs::path path = dir / "valid.m4a";
    check("write valid M4A", writeFile(path, valid));
    SoundBuffer buffer;
    const auto result = buffer.load(path);
    check("valid M4A decodes after failures", static_cast<bool>(result));
    // The fixture is mono, but faad (the AAC decoder without gst-libav, as on
    // CI) upmixes mono to stereo, so accept either layout.
    check("valid M4A has 44100 Hz samples",
          (buffer.channels == 1 || buffer.channels == 2) && buffer.sampleRate == 44100 &&
          buffer.numSamples >= 4096 &&
          buffer.samples.size() == buffer.numSamples * static_cast<size_t>(buffer.channels));
    double energy = 0;
    for (float sample : buffer.samples) energy += sample * sample;
    check("valid M4A contains finite non-silent audio", std::isfinite(energy) && energy > 1);
    fs::remove_all(dir);
    return failures ? 1 : 0;
#else
    std::printf("SKIP: Linux GStreamer AAC regression\n");
    return 0;
#endif
}
