#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#if defined(__linux__) && !defined(__ANDROID__)
#include "../../jpegDecode/fixtures.h"
#include "impl/tcImageDecode.h"
#include <fstream>
#include <unistd.h>

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}
#endif
TC_CORE_TEST_MAIN() {
#if defined(__linux__) && !defined(__ANDROID__)
    using namespace tc;
    const auto bytes = jpegTest::encode(97, 65);
    Pixels pixels;
    check("Pixels JPEG memory load", bool(pixels.loadFromMemory(bytes.data(), int(bytes.size()))) &&
        pixels.getWidth() == 97 && pixels.getHeight() == 65 && pixels.getChannels() == 4);
    std::vector<unsigned char> expected(97 * 65 * 4);
    internal::JpegDecode jpeg;
    check("shared helper decodes", jpeg.open(bytes.data(), bytes.size()) && jpeg.decode(expected.data(), expected.size()));
    check("Pixels uses native JPEG output", pixels.isAllocated() &&
        std::memcmp(pixels.getData(), expected.data(), expected.size()) == 0);
    const auto stripped = jpegTest::stripDht(bytes);
    check("Pixels accepts DHT-less JPEG", bool(pixels.loadFromMemory(stripped.data(), int(stripped.size()))) &&
        std::memcmp(pixels.getData(), expected.data(), expected.size()) == 0);
    // A non-JPEG extension proves routing uses the signature.
    const auto path = fs::temp_directory_path() / ("tc-jpeg-" + std::to_string(getpid()) + ".dat");
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    Image image;
    check("Image file load works without a display", bool(image.load(path)) &&
        image.getWidth() == 97 && image.getHeight() == 65 &&
        std::memcmp(image.getPixels().getData(), expected.data(), expected.size()) == 0);
    Texture texture;
    texture.allocate(pixels);
    check("JPEG pixels feed headless texture path", !texture.isAllocated() && !sg_isvalid());
    Pixels linear;
    check("loadHDR also routes JPEG to native decoder", bool(linear.loadHDR(path)) && linear.isFloat());
    if (linear.isAllocated()) {
        bool matches = true;
        for (size_t i = 0; i < expected.size(); ++i) {
            const float value = i % 4 == 3 ? 1.0f : std::pow(expected[i] / 255.0f, 2.2f);
            matches &= std::abs(linear.getDataF32()[i] - value) < 0.00001f;
        }
        check("HDR gamma and alpha retained", matches);
    }
    // CMYK, Adobe-inverted CMYK and YCCK must use the same bytes as other platforms.
    for (auto [adobe, ycck] : {std::pair{true, false}, {false, false}, {true, true}}) {
        const auto cmyk = jpegTest::cmyk(adobe, ycck);
        internal::JpegDecode camera;
        check("camera rejects CMYK/YCCK without stb fallback", !camera.open(cmyk.data(), cmyk.size()) &&
            camera.needsStbFallback());
        int w = 0, h = 0, channels = 0;
        auto* stb = stbi_load_from_memory(cmyk.data(), int(cmyk.size()), &w, &h, &channels, 4);
        check("CMYK/YCCK stb fixture decodes", stb && w == 8 && h == 8);
        check("CMYK/YCCK Image memory equals stb byte for byte",
            image.loadFromMemory(cmyk.data(), int(cmyk.size())) && image.getWidth() == 8 &&
            image.getHeight() == 8 && stb && std::memcmp(image.getPixelsData(), stb, 8 * 8 * 4) == 0);
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(cmyk.data()), cmyk.size());
        }
        check("CMYK/YCCK Image file equals stb byte for byte", image.load(path) && stb &&
            std::memcmp(image.getPixelsData(), stb, 8 * 8 * 4) == 0);
        stbi_image_free(stb);
        auto* stbFloat = stbi_loadf_from_memory(cmyk.data(), int(cmyk.size()), &w, &h, &channels, 3);
        bool matches = linear.loadHDR(path) && stbFloat && linear.isFloat();
        if (matches) for (size_t i = 0; i < 8 * 8; ++i) {
            matches &= std::memcmp(linear.getDataF32() + i * 4, stbFloat + i * 3, 3 * sizeof(float)) == 0;
            matches &= linear.getDataF32()[i * 4 + 3] == 1.0f;
        }
        check("CMYK/YCCK HDR equals stb RGB conversion", matches);
        stbi_image_free(stbFloat);
    }
    const auto huge = jpegTest::withDimensions(jpegTest::encode(8, 8), 60000, 60000);
    int w = 0, h = 0, channels = 0;
    auto* stb = stbi_load_from_memory(huge.data(), int(huge.size()), &w, &h, &channels, 4);
    check("60000x60000 stb rejects as too large", !stb && std::string(stbi_failure_reason()) == "too large");
    stbi_image_free(stb);
    internal::failImageDecodeAllocationAfterForTests(0);
    const auto rejected = image.loadFromMemory(huge.data(), int(huge.size()));
    check("60000x60000 keeps stb error class with no pixel allocation",
        rejected.error == LoadError::DecodeFailed && rejected.message == "too large" && !image.isAllocated() &&
        internal::imageDecodeAllocationAttemptsForTests() == 0);
    internal::failImageDecodeAllocationAfterForTests(-1);
    const auto rgbaHuge = jpegTest::withDimensions(jpegTest::encode(8, 8), 32768, 16384);
    internal::failImageDecodeAllocationAfterForTests(0);
    const auto rgbaRejected = pixels.loadFromMemory(rgbaHuge.data(), int(rgbaHuge.size()));
    check("RGB header fits but RGBA plus extra byte exceeds stb int limit",
        rgbaRejected.error == LoadError::DecodeFailed && !pixels.isAllocated() &&
        internal::imageDecodeAllocationAttemptsForTests() == 0);
    internal::failImageDecodeAllocationAfterForTests(-1);
    // RGB bytes fit INT_MAX, but stb's three-channel float conversion does not.
    const auto hdrHuge = jpegTest::withDimensions(jpegTest::encode(8, 8), 14000, 14000);
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(hdrHuge.data()), hdrHuge.size());
    }
    internal::failImageDecodeAllocationAfterForTests(1); // Encoded snapshot only.
    const auto hdrRejected = linear.loadHDR(path);
    check("HDR keeps stb float size limit before pixel allocation",
        hdrRejected.error == LoadError::DecodeFailed && !linear.isAllocated() &&
        internal::imageDecodeAllocationAttemptsForTests() == 1);
    internal::failImageDecodeAllocationAfterForTests(-1);
    fs::remove(path);
    check("truncated JPEG fails without publishing pixels", !pixels.loadFromMemory(bytes.data(), int(bytes.size() - 2)) &&
        !pixels.isAllocated());
    internal::failImageDecodeAllocationAfterForTests(0);
    check("output allocation failure is reported", !pixels.loadFromMemory(bytes.data(), int(bytes.size())) && !pixels.isAllocated());
    internal::failImageDecodeAllocationAfterForTests(-1);
    return failures ? 1 : 0;
#else
    std::puts("SKIP: Linux JPEG integration");
    return 77;
#endif
}
