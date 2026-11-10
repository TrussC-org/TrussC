#include <cstdio>
#if defined(__linux__) && !defined(__ANDROID__)
#include "fixtures.h"
#include <chrono>
#include <cstring>
#include <cstdint>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define STBI_NEON
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
bool decode(const std::vector<unsigned char>& bytes, std::vector<unsigned char>& rgba, int width, int height) {
    trussc::internal::JpegDecode jpeg;
    return jpeg.open(bytes.data(), bytes.size()) && jpeg.width() == width && jpeg.height() == height &&
        jpeg.decode(rgba.data(), rgba.size());
}
void correctness() {
    constexpr int width = 97, height = 65;
    std::vector<unsigned char> rgba(size_t(width) * height * 4);
    for (int kind = 0; kind < 3; ++kind) {
        auto bytes = jpegTest::encode(width, height, kind == 1, kind == 2);
        check("decode dimensions (baseline / gray / progressive)", decode(bytes, rgba, width, height));
        int w = 0, h = 0, channels = 0;
        auto* stb = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
        check("stb comparison dimensions", stb && w == width && h == height);
        if (stb && w == width && h == height) {
            int maxDifference = 0;
            uint64_t total = 0;
            for (size_t i = 0; i < rgba.size(); ++i) {
                const int difference = std::abs(int(rgba[i]) - int(stb[i]));
                maxDifference = std::max(maxDifference, difference);
                total += difference;
            }
            const double mean = double(total) / rgba.size();
            std::printf("kind=%d max_channel_difference=%d mean_error=%.6f\n", kind, maxDifference, mean);
            check("mean channel error <= 2 levels", mean <= 2.0);
        }
        stbi_image_free(stb);
        const auto expected = rgba;
        bool rejected = true;
        for (size_t length = 0; length < bytes.size(); ++length) {
            const std::vector<unsigned char> prefix(bytes.begin(), bytes.begin() + length);
            rejected &= !decode(prefix, rgba, width, height);
        }
        check("every truncated prefix rejected", rejected);
        if (kind == 0) {
            auto stripped = jpegTest::stripDht(bytes);
            check("DHT removed", stripped.size() < bytes.size());
            check("native DHT-less MJPEG decode", decode(stripped, rgba, width, height));
            check("DHT-less output matches original", rgba == expected);
            trussc::internal::JpegDecode shortOutput;
            std::fill(rgba.begin(), rgba.end(), 0x5a);
            check("undersized output rejected before writes", shortOutput.open(bytes.data(), bytes.size()) &&
                !shortOutput.decode(rgba.data(), rgba.size() - 1) &&
                std::all_of(rgba.begin(), rgba.end(), [](auto b) { return b == 0x5a; }));
            // Extraneous bytes before EOI cause a warning, but are recoverable.
            bytes.insert(bytes.end() - 2, {0x12, 0x34});
            check("recoverable warning is not fatal", decode(bytes, rgba, width, height));
        }
    }
    for (const auto& bytes : {std::vector<unsigned char>{0xff,0xd8,0xff,0xd9},
                             std::vector<unsigned char>{0xff,0xd8,0xff,0xe0,0,1},
                             std::vector<unsigned char>{0,1,2,3}})
        check("malformed input rejected", !decode(bytes, rgba, width, height));
}
void benchmark(int frames) {
#ifdef STBI_NEON
    std::puts("stb SIMD: NEON");
#elif defined(STBI_SSE2)
    std::puts("stb SIMD: SSE2");
#else
    std::puts("stb SIMD: scalar");
#endif
    for (const auto& size : {std::pair{640,480}, {1280,720}, {1920,1080}}) {
        const auto bytes = jpegTest::encode(size.first, size.second);
        std::vector<unsigned char> rgba(size_t(size.first) * size.second * 4);
        std::vector<double> turboTimes, stbTimes;
        for (int iteration = 0; iteration < 3; ++iteration) {
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < frames; ++i)
                if (!decode(bytes, rgba, size.first, size.second)) { check("bench decode", false); return; }
            turboTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames);
            start = std::chrono::steady_clock::now();
            for (int i = 0; i < frames; ++i) {
                int w, h, channels;
                auto* decoded = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
                if (!decoded) { check("bench stb decode", false); return; }
                stbi_image_free(decoded);
            }
            stbTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames);
        }
        std::sort(turboTimes.begin(), turboTimes.end());
        std::sort(stbTimes.begin(), stbTimes.end());
        std::printf("%dx%d q85 median_of_3 frames=%d libjpeg_ms=%.4f stb_ms=%.4f speedup=%.2f\n",
            size.first, size.second, frames, turboTimes[1], stbTimes[1], stbTimes[1] / turboTimes[1]);
    }
}
}
int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0)
        benchmark(argc >= 3 ? std::max(1, std::atoi(argv[2])) : 100);
    else correctness();
    return failures ? 1 : 0;
}
#else
int main() { std::puts("SKIP: system libjpeg decoder is Linux-only"); return 77; }
#endif
