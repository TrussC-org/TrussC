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
