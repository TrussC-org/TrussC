#include <TrussC.h>
#include "impl/tcImageDecode.h"
#include "tc/utils/tcAnalyzeImage.h"
#include "../../common/tcCoreTest.h"
#include <climits>
#include <fstream>
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#endif

// Owned binary: fail reporting allocations as well as decoder/output storage.
namespace { thread_local bool failNew = false; }
#ifdef TC_TEST_WRAP_HEAP
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t n) { return failNew ? nullptr : __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t m) { return failNew ? nullptr : __real_calloc(n, m); }
extern "C" void* __wrap_realloc(void* p, size_t n) { return failNew ? nullptr : __real_realloc(p, n); }
#endif
void* operator new(std::size_t n) {
    if (failNew) throw std::bad_alloc();
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    return failNew ? nullptr : std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

using namespace std;
using namespace tc;
namespace {
using Bytes = vector<unsigned char>;
int failures = 0;
void check(const string& label, bool ok) {
    printf("%-76s %s\n", label.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++failures;
}
void append(void* context, void* bytes, int count) {
    auto& output = *static_cast<Bytes*>(context);
    const auto* data = static_cast<const unsigned char*>(bytes);
    output.insert(output.end(), data, data + count);
}
Bytes encoded(const string& format, int w, int h) {
    Bytes output;
    Bytes pixels(size_t(w) * h * 4, 255);
    if (format == "png") stbi_write_png_to_func(append, &output, w, h, 4, pixels.data(), w * 4);
    if (format == "bmp") stbi_write_bmp_to_func(append, &output, w, h, 4, pixels.data());
    if (format == "jpeg") stbi_write_jpg_to_func(append, &output, w, h, 4, pixels.data(), 90);
    if (format == "hdr") {
        vector<float> values(size_t(w) * h * 3, 1.0f);
        stbi_write_hdr_to_func(append, &output, w, h, 3, values.data());
    }
    return output;
}
void put32(Bytes& bytes, size_t position, uint32_t value, bool bigEndian) {
    for (int i = 0; i < 4; ++i) bytes[position + i] = (value >> (8 * (bigEndian ? 3 - i : i))) & 255;
}
void be32(Bytes& bytes, uint32_t value) {
    const auto start = bytes.size(); bytes.resize(start + 4); put32(bytes, start, value, true);
}
void chunk(Bytes& png, const char* name, const Bytes& contents) {
    be32(png, uint32_t(contents.size()));
    const auto start = png.size();
    png.insert(png.end(), name, name + 4);
    png.insert(png.end(), contents.begin(), contents.end());
    uint32_t crc = 0xffffffffu;
    for (size_t i = start; i < png.size(); ++i) {
        crc ^= png[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    be32(png, ~crc);
}
// Small stored PNG rows exercise packed samples and Adam7 without a fixture file.
Bytes scanlinePng(bool interlaced, bool sixteenBit, bool extraRow = false) {
    const int w = 8, h = 8, element = sixteenBit ? 2 : 1;
    Bytes raw;
    auto pass = [&](int columns, int rows) {
        if (!columns || !rows) return;
        for (int y = 0; y < rows; ++y) {
            raw.push_back(0);
            raw.insert(raw.end(), size_t(columns) * 3 * element, 255);
        }
    };
    if (interlaced) {
        const int xorig[] = {0,4,0,2,0,1,0}, yorig[] = {0,0,4,0,2,0,1};
        const int xspace[] = {8,8,4,4,2,2,1}, yspace[] = {8,8,8,4,4,2,2};
        for (int p = 0; p < 7; ++p) pass((w - xorig[p] + xspace[p] - 1) / xspace[p],
                                        (h - yorig[p] + yspace[p] - 1) / yspace[p]);
    } else pass(w, h);
    if (extraRow) pass(w, 1);
    Bytes z = {0x78, 1, 1, static_cast<unsigned char>(raw.size()), static_cast<unsigned char>(raw.size() >> 8),
               static_cast<unsigned char>(~raw.size()), static_cast<unsigned char>((~raw.size()) >> 8)};
    z.insert(z.end(), raw.begin(), raw.end());
    uint32_t a = 1, b = 0;
    for (const auto value : raw) { a = (a + value) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    Bytes png = {137,80,78,71,13,10,26,10}, ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {static_cast<unsigned char>(sixteenBit ? 16 : 8), 2, 0, 0,
                            static_cast<unsigned char>(interlaced)});
    chunk(png, "IHDR", ihdr); chunk(png, "IDAT", z); chunk(png, "IEND", {});
    return png;
}
// Complete zero-filled PNG with fixed-Huffman DEFLATE matches. The fixture
// needs only compressed storage, even for a billion-byte decoded canvas.
Bytes zeroPng(uint32_t width, uint32_t height, bool cgbi = false) {
    const uint64_t count = (uint64_t(width) * 4 + 1) * height;
    Bytes z{0x78, 0x01};
    unsigned bits = 0, used = 0;
    auto emit = [&](unsigned value, unsigned n) {
        bits |= value << used; used += n;
        while (used >= 8) { z.push_back(bits & 255); bits >>= 8; used -= 8; }
    };
    auto symbol = [&](unsigned code) {
        unsigned value = code <= 143 ? 0x30 + code : code <= 255 ? 0x190 + code - 144 :
                         code <= 279 ? code - 256 : 0xc0 + code - 280;
        unsigned n = code <= 143 ? 8 : code <= 255 ? 9 : code <= 279 ? 7 : 8;
        unsigned reversed = 0;
        for (unsigned i = 0; i < n; ++i) { reversed = (reversed << 1) | (value & 1); value >>= 1; }
        emit(reversed, n);
    };
    emit(3, 3); // final fixed-Huffman block
    symbol(0);
    uint64_t remaining = count - 1;
    while (remaining >= 258) { symbol(285); emit(0, 5); remaining -= 258; }
    while (remaining--) symbol(0);
    symbol(256);
    if (used) z.push_back(bits & 255);
    be32(z, uint32_t((count % 65521) << 16) | 1); // Adler32 of zeros
    if (cgbi) z = Bytes(z.begin() + 2, z.end() - 4); // CgBI uses raw DEFLATE
    Bytes png{137,80,78,71,13,10,26,10}, ihdr;
    if (cgbi) chunk(png, "CgBI", {0x40,0,0,0});
    be32(ihdr, width); be32(ihdr, height);
    ihdr.insert(ihdr.end(), {8,6,0,0,0});
    chunk(png, "IHDR", ihdr); chunk(png, "IDAT", z); chunk(png, "IEND", {});
    return png;
}
void writeFile(const fs::path& path, const Bytes& bytes) {
    ofstream output(path, ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), streamsize(bytes.size()));
    check("fixture write", bool(output));
}
bool empty(const Pixels& pixels) {
    return !pixels.isAllocated() && !pixels.getDataVoid() && pixels.getWidth() == 0 &&
           pixels.getHeight() == 0 && pixels.getChannels() == 0;
}
void callbackInputChecks() {
    struct VirtualPpm {
        size_t commentBytes;
        mutable size_t generated = 0;
    };
    const string prefix = "P6\n#", suffix = "\n1 1\n255\n\xff\xff\xff";
    for (bool asFloat : {false, true}) {
        for (bool large : {false, true}) {
            VirtualPpm source{large ? size_t(INT_MAX) + 1024 : 16};
            internal::ImageCallbackInput input{&source, prefix.size() + source.commentBytes + suffix.size(),
                [](const void* context, size_t position, char* output, size_t count) {
                    const auto& source = *static_cast<const VirtualPpm*>(context);
                    constexpr char prefix[] = "P6\n#", suffix[] = "\n1 1\n255\n\xff\xff\xff";
                    std::memset(output, 'x', count);
                    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
                        if (i >= position && i - position < count) output[i - position] = prefix[i];
                    const size_t tail = sizeof(prefix) - 1 + source.commentBytes;
                    for (size_t i = 0; i < sizeof(suffix) - 1; ++i)
                        if (tail + i >= position && tail + i - position < count) output[tail + i - position] = suffix[i];
                    source.generated += count;
                }};
            vector<string> errors;
            auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
                if (event.level == LogLevel::Error) errors.push_back(event.message);
            });
            int width = 0, height = 0, channels = 0;
            unique_ptr<void, decltype(&stbi_image_free)> decoded(input.decode(asFloat, width, height, channels), stbi_image_free);
            const auto result = input.result("virtual-long-comment.ppm");
            if (large) {
                check("virtual input over INT_MAX fails cleanly", !decoded && result.error == LoadError::TooLarge);
                check("callback delivery reserves stb's refill buffer", input.delivered == size_t(INT_MAX) - 128 &&
                      source.generated == input.delivered);
                check("callback limit emits exactly one Error with filename and reason", errors.size() == 1 &&
                      errors[0].find("virtual-long-comment.ppm") != string::npos &&
                      errors[0].find("stb_image counts input with a 32-bit int") != string::npos &&
                      result.message.find("stb_image counts input with a 32-bit int") != string::npos);
            } else {
                check("small virtual image decodes through the same callback adapter", result && decoded &&
                      width == 1 && height == 1 && channels == 3 && errors.empty() && !input.limitReached);
                if (decoded) check("small callback image retains its pixel", asFloat ?
                    static_cast<float*>(decoded.get())[2] == 1.0f : static_cast<unsigned char*>(decoded.get())[3] == 255);
            }
        }
    }
}
void rejected(const string& label, const Bytes& bytes, LoadError error = LoadError::TooLarge) {
    Pixels pixels;
    pixels.allocate(1, 1);
    const auto before = internal::imageDecodeRejectionsForTests();
    const auto result = pixels.loadFromMemory(bytes.data(), int(bytes.size()));
    check(label, !result && result.error == error && empty(pixels));
    check(label + " counts only admission failures", internal::imageDecodeRejectionsForTests() == before + (error == LoadError::TooLarge ? 1 : 0));
}
void boundaryChecks(const fs::path& root) {
    Pixels pixels;
    internal::recordImageDecodeLimit(0);
    check("headless axis fallback is 16384", internal::imageDecodeAxisLimit() == 16384);
    auto edge = encoded("png", 16384, 1);
    check("headless exactly-at-limit image loads", pixels.loadFromMemory(edge.data(), int(edge.size())) && pixels.getWidth() == 16384);
    edge = encoded("png", 16385, 1);
    writeFile(root / "wide.png", edge);
    check("CPU local image exceeds old headless axis cap", pixels.load(root / "wide.png") && pixels.getWidth() == 16385);
    check("CPU memory image exceeds old headless axis cap", pixels.loadFromMemory(edge.data(), int(edge.size())) && pixels.getWidth() == 16385);
    internal::recordImageDecodeLimit(64);
    writeFile(root / "over.png", encoded("png", 65, 1));
    check("CPU load ignores recorded GPU limit", pixels.load(root / "over.png") && pixels.getWidth() == 65);
    writeFile(root / "float-image", encoded("hdr", 65, 1));
    check("CPU float load ignores recorded GPU limit", pixels.loadHDR(root / "float-image") && pixels.isFloat() && pixels.getWidth() == 65);
    Pixels mcpPixels;
    const auto beforeMcp = internal::imageDecodeRejectionsForTests();
    check("MCP local file follows CPU loading", mcp::internal::loadMcpImage(mcpPixels, root / "over.png") &&
          mcpPixels.getWidth() == 65 && internal::imageDecodeRejectionsForTests() == beforeMcp);
}
void headerChecks() {
    rejected("corrupt bytes", Bytes{1,2,3}, LoadError::DecodeFailed);
    Pixels pixels;
    const auto invalidBefore = internal::imageDecodeRejectionsForTests();
    const unsigned char byte = 0;
    check("negative memory length", !pixels.loadFromMemory(&byte, -1) && empty(pixels));
    check("null memory input", !pixels.loadFromMemory(nullptr, 10) && empty(pixels));
    check("empty memory input", !pixels.loadFromMemory(&byte, 0) && empty(pixels));
    check("invalid arguments count no bound loss", internal::imageDecodeRejectionsForTests() == invalidBefore);
    const auto productsBefore = internal::imageDecodeRejectionsForTests();
    check("checked source channel product", internal::admitImageDimensions(64, 64, SIZE_MAX, sizeof(float)).error == LoadError::TooLarge);
    check("checked float element product", internal::admitImageDimensions(64, 64, 4, SIZE_MAX).error == LoadError::TooLarge);
    check("unrepresentable products count bound losses", internal::imageDecodeRejectionsForTests() == productsBefore + 2);
    size_t count = 0;
    check("32-bit float image product rejects overflow", !internal::pixelBufferCount(16384, 16384, 4, sizeof(float), UINT32_MAX, count));
    for (bool interlaced : {false, true}) for (bool sixteenBit : {false, true}) {
        const auto png = scanlinePng(interlaced, sixteenBit);
        check("PNG scanlines decode completely", pixels.loadFromMemory(png.data(), int(png.size())) && pixels.getColor(7, 7) == Color(1, 1, 1, 1));
    }
    auto topDown = encoded("bmp", 8, 8);
    put32(topDown, 22, uint32_t(-8), false);
    check("top-down BMP remains supported", pixels.loadFromMemory(topDown.data(), int(topDown.size())) && pixels.getHeight() == 8);
    put32(topDown, 22, 0x80000000u, false);
    rejected("unrepresentable BMP height", topDown, LoadError::DecodeFailed);
    rejected("PNG scanline mismatch", scanlinePng(false, false, true), LoadError::DecodeFailed);
    auto truncated = encoded("png", 1, 1);
    for (size_t i = 4; i + 4 < truncated.size(); ++i) if (memcmp(truncated.data() + i, "IDAT", 4) == 0) {
        put32(truncated, i - 4, 0x10000000, true); break;
    }
    internal::failImageDecodeAllocationAfterForTests(-1);
    rejected("PNG incomplete chunk span", truncated, LoadError::DecodeFailed);
    check("PNG IDAT span checked before size-driven allocation", internal::imageDecodeLargestAllocationForTests() < 1024 * 1024);
}
void earlyPngChecks(const fs::path& root) {
    Pixels pixels;
    for (bool cgbi : {false, true}) {
        auto png = zeroPng(8, 8, cgbi);
        // stb ignores CRCs; local and memory decoding now use that policy.
        png[cgbi ? 45 : 29] ^= 1;
        check("PNG with unusual IHDR CRC follows stb", pixels.loadFromMemory(png.data(), int(png.size())) &&
              pixels.getWidth() == 8 && pixels.getHeight() == 8);
        const auto path = root / "unusual-crc.png";
        writeFile(path, png);
        check("local PNG CRC follows stb", pixels.load(path) && pixels.getWidth() == 8);
        Pixels mcpPixels;
        check("MCP local PNG follows stb CRC policy", mcp::internal::loadMcpImage(mcpPixels, path) && mcpPixels.getWidth() == 8);
    }
}
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
void gpuReplacementChecks(const fs::path& root) {
    // Resolve EGL at runtime so other platforms retain the same owned test.
    auto* library = dlopen("libEGL.so.1", RTLD_NOW);
    check("surfaceless EGL library is available", library != nullptr);
    if (!library) return;
#define EGL_FUNCTION(name) auto name = reinterpret_cast<decltype(&::name)>(dlsym(library, #name))
    EGL_FUNCTION(eglGetPlatformDisplay); EGL_FUNCTION(eglInitialize); EGL_FUNCTION(eglBindAPI);
    EGL_FUNCTION(eglChooseConfig); EGL_FUNCTION(eglCreateContext); EGL_FUNCTION(eglMakeCurrent);
    EGL_FUNCTION(eglDestroyContext); EGL_FUNCTION(eglTerminate); EGL_FUNCTION(eglGetError);
#undef EGL_FUNCTION
    auto stage = [&](const char* name, bool ok) {
        if (!ok) printf("surfaceless EGL %s failed: eglGetError=0x%04x\n", name, eglGetError());
        return ok;
    };
    auto display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    const bool haveDisplay = stage("eglGetPlatformDisplay", display != EGL_NO_DISPLAY);
    EGLint major = 0, minor = 0, count = 0;
#if defined(SOKOL_GLES3)
    constexpr EGLenum api = EGL_OPENGL_ES_API;
    const EGLint attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    const EGLint contextAttributes[]{EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
#else // SOKOL_GLCORE and the existing desktop EGL fallback for other backends.
    constexpr EGLenum api = EGL_OPENGL_API;
    const EGLint attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    const EGLint contextAttributes[]{EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE};
#endif
    EGLConfig config{};
    const bool initialized = haveDisplay && stage("eglInitialize", eglInitialize(display, &major, &minor)) &&
        stage("eglBindAPI", eglBindAPI(api)) &&
        stage("eglChooseConfig", eglChooseConfig(display, attributes, &config, 1, &count)) &&
        stage("eglChooseConfig (no matching config)", count != 0);
    auto context = initialized ? eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes) : EGL_NO_CONTEXT;
    const bool created = initialized && stage("eglCreateContext", context != EGL_NO_CONTEXT);
    const bool current = created && stage("eglMakeCurrent",
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
    check("surfaceless EGL context is available", current);
    if (!current) {
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (haveDisplay) eglTerminate(display);
        dlclose(library); return;
    }
    sg_desc descriptor{}; sg_setup(&descriptor);
    internal::recordImageDecodeLimit(sg_query_limits().max_image_size_2d);
    {
        const auto wide = encoded("png", sg_query_limits().max_image_size_2d + 1, 1);
        Image oversized;
        check("GPU upload still rejects device axis overflow", oversized.loadFromMemory(wide.data(), int(wide.size())).error == LoadError::TooLarge &&
              !oversized.isAllocated() && !oversized.getTexture().isAllocated());
    }
    const auto bytes = encoded("png", 8, 8);
    const auto path = root / "gpu-replacement.png";
    writeFile(path, bytes);
    internal::appRunToken() = make_shared<atomic<bool>>(true);
    // Exercise both initially loaded and dynamically allocated Images. Repeat
    // with pending retirement: cleanup must stay allocation-free.
    for (int cycle = 0; cycle < 8; ++cycle) {
        Image image;
        if (cycle & 1) image.allocate(8, 8); else image.loadFromMemory(bytes.data(), int(bytes.size()));
        check("replacement starts with GPU pixels and texture", image.isAllocated() && image.getTexture().isAllocated());
        const auto oldImage = image.getTexture().getImage();
        const auto before = internal::imageDecodeRejectionsForTests();
        LoadResult result; bool escaped = false;
        failNew = true;
        try { result = cycle & 1 ? image.load(path) : image.loadFromMemory(bytes.data(), int(bytes.size())); }
        catch (...) { escaped = true; }
        failNew = false;
        check("GPU sync replacement OOM returns clean empty image", !escaped && result.error == LoadError::Unknown &&
              !image.isAllocated() && !image.getTexture().isAllocated() && internal::imageDecodeRejectionsForTests() == before);
        check("retirement keeps old GPU handle alive until collection", sg_query_image_state(oldImage) == SG_RESOURCESTATE_VALID);
        image.clear();
        internal::drainPendingGpuDestroys();
        check("retired GPU image is collected", sg_query_image_state(oldImage) == SG_RESOURCESTATE_INVALID);
    }
    internal::appRunToken()->store(false);
    internal::drainPendingGpuDestroys(); sg_shutdown();
    internal::recordImageDecodeLimit(0);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context); eglTerminate(display); dlclose(library);
}
#endif
void inputSizeChecks(const fs::path& root) {
    constexpr size_t cap = 64 * 1024 * 1024;
    Pixels pixels;
    writeFile(root / "empty.png", {});
    check("empty file rejects without an output", pixels.load(root / "empty.png").error == LoadError::DecodeFailed && empty(pixels));
    const unsigned char byte = 0;
    internal::failImageDecodeAllocationAfterForTests(-1);
    check("memory size rejected before reading", pixels.loadFromMemory(&byte, int(cap + 1)).error == LoadError::TooLarge && empty(pixels) &&
        internal::imageDecodeAllocationAttemptsForTests() == 0);
    const auto path = root / "encoded.ppm";
    {
        ofstream file(path, ios::binary);
        file << "P6\n5000 4667\n255\n";
        const Bytes row(5000 * 3, 255);
        for (int y = 0; y < 4667; ++y)
            file.write(reinterpret_cast<const char*>(row.data()), streamsize(row.size()));
        check("70 MB local PPM fixture written", bool(file));
    }
    check("local image above 64 MiB loads completely", pixels.load(path) && pixels.getWidth() == 5000 &&
          pixels.getHeight() == 4667 && pixels.getColor(4999, 4666) == Color(1, 1, 1, 1));
    pixels.clear();
    {
        Image image;
        check("headless Image loads local image above 64 MiB", image.load(path) && image.getWidth() == 5000);
        image.clear();
        internal::appRunToken() = make_shared<atomic<bool>>(true);
        int events = 0;
        auto listener = image.onLoad.listen([&](LoadResult& result) {
            check("async local image above 64 MiB loads completely", result && image.getHeight() == 4667 &&
                  image.getPixels().getColor(4999, 4666) == Color(1, 1, 1, 1));
            ++events;
        });
        image.loadAsync(path);
        internal::waitForResourceLoadsForTests();
        internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
        check("async large local load completes once", events == 1 && !image.isLoading());
        internal::appRunToken()->store(false);
    }
    fs::remove(path);
    auto bytes = encoded("png", 1, 1);
    bytes.resize(cap, 0);
    check("exact encoded input cap loads", pixels.loadFromMemory(bytes.data(), int(bytes.size())) && pixels.getWidth() == 1);
    writeFile(path, bytes);
    check("exact encoded file cap loads", pixels.load(path) && pixels.getWidth() == 1);
}
#ifdef __APPLE__
Bytes grayTiff(int width) {
    Bytes bytes(122 + width, 0);
    auto le16 = [&](size_t at, unsigned value) { bytes[at] = value & 255; bytes[at + 1] = (value >> 8) & 255; };
    bytes[0] = bytes[1] = 'I'; le16(2, 42); put32(bytes, 4, 8, false); le16(8, 9);
    size_t entry = 10;
    auto tag = [&](unsigned key, unsigned type, unsigned value) {
        le16(entry, key); le16(entry + 2, type); put32(bytes, entry + 4, 1, false);
        put32(bytes, entry + 8, value, false); entry += 12;
    };
    tag(256, 4, width); tag(257, 4, 1); tag(258, 3, 8); tag(259, 3, 1);
    tag(262, 3, 1); tag(273, 4, 122); tag(277, 3, 1); tag(278, 4, 1); tag(279, 4, width);
    fill(bytes.begin() + 122, bytes.end(), 255);
    return bytes;
}
void platformChecks(const fs::path& root) {
    writeFile(root / "edge.tiff", grayTiff(64));
    writeFile(root / "over.tiff", grayTiff(65));
    Pixels pixels;
    check("ImageIO-only format loads at boundary", pixels.load(root / "edge.tiff") &&
          pixels.getWidth() == 64 && pixels.getColor(63, 0) == Color(1, 1, 1, 1));
    check("direct ImageIO exceeds old axis cap", pixels.loadPlatform(root / "over.tiff") && pixels.getWidth() == 65);
    check("ImageIO file fallback exceeds old axis cap", pixels.load(root / "over.tiff") && pixels.getWidth() == 65);
    pixels.clear();
    check("MCP ImageIO fallback follows local loading", mcp::internal::loadMcpImage(pixels, root / "over.tiff") && pixels.getWidth() == 65);
}
#endif
void mipSamplingChecks() {
    for (bool asFloat : {false, true}) for (auto dimensions : {pair{8, 8}, pair{7, 3}, pair{1, 8}}) {
        Pixels source;
        source.allocate(dimensions.first, dimensions.second, 4, asFloat ? PixelFormat::F32 : PixelFormat::U8);
        for (int y = 0; y < source.getHeight(); ++y) for (int x = 0; x < source.getWidth(); ++x)
            source.setColor(x, y, Color((x % 2) ? 1.f : 0.f, (y % 2) ? 1.f : 0.f, .25f, .5f));
        auto expected = source.clone();
        expected.halve();
        Pixels actual;
        check("image-load mip storage succeeds", internal::ImageLoadAccess::mipLevel(actual, source));
        check("image-load mip retains gamma/float and edge sampling", actual.getWidth() == expected.getWidth() &&
              actual.getHeight() == expected.getHeight() && actual.getTotalBytes() == expected.getTotalBytes() &&
              memcmp(actual.getDataVoid(), expected.getDataVoid(), actual.getTotalBytes()) == 0);
    }
}
void allocationChecks(const fs::path& root) {
    // Include GIF's first-frame output/background/history and HDR float scratch.
    const Bytes gif = {'G','I','F','8','9','a',1,0,1,0,0x80,0,0,0,0,0,255,255,255,
                       ',',0,0,0,0,1,0,1,0,0,2,2,0x4c,1,0,';'};
    vector<Bytes> cases{encoded("png", 8, 8), encoded("bmp", 8, 8), encoded("jpeg", 8, 8),
                        encoded("hdr", 8, 8), gif, scanlinePng(true, true)};
    for (size_t index = 0; index < cases.size(); ++index) {
        const auto& bytes = cases[index];
        Pixels pixels;
        internal::failImageDecodeAllocationAfterForTests(-1);
        check("allocation fixture decodes", bool(pixels.loadFromMemory(bytes.data(), int(bytes.size()))));
        const auto allocations = internal::imageDecodeAllocationAttemptsForTests();
        for (size_t step = 0; step < allocations; ++step) {
            internal::failImageDecodeAllocationAfterForTests(int64_t(step));
            const auto before = internal::imageDecodeRejectionsForTests();
            const auto result = pixels.loadFromMemory(bytes.data(), int(bytes.size()));
            check("injected decoder/output allocation failure is whole", result.error == LoadError::Unknown && empty(pixels) &&
                  internal::imageDecodeRejectionsForTests() == before);
        }
    }
    internal::failImageDecodeAllocationAfterForTests(-1);
    const auto png = encoded("png", 8, 8);
    if (!tcTest::skipPersistentAllocationFailure("image OOM result/reporting")) {
        // No test assertions or fixture construction while new is disabled.
        Pixels exhausted;
        failNew = true;
        auto emergency = internal::imageDecodeOutOfMemory();
        auto exhaustedResult = exhausted.loadFromMemory(png.data(), int(png.size()));
        failNew = false;
        check("OOM reporting survives failed message and Logger allocations", emergency.error == LoadError::Unknown &&
              exhaustedResult.error == LoadError::Unknown && empty(exhausted));
    }
    writeFile(root / "mcp-allocation.png", png);
    Pixels mcpPixels;
    internal::failImageDecodeAllocationAfterForTests(-1);
    check("MCP allocation fixture loads", mcp::internal::loadMcpImage(mcpPixels, root / "mcp-allocation.png"));
    const auto mcpAllocations = internal::imageDecodeAllocationAttemptsForTests();
    for (size_t step = 0; step < mcpAllocations; ++step) {
        mcpPixels.clear();
        internal::failImageDecodeAllocationAfterForTests(int64_t(step));
        const auto before = internal::imageDecodeRejectionsForTests();
        check("MCP encoded/decoder/pixel OOM returns failure", !mcp::internal::loadMcpImage(mcpPixels, root / "mcp-allocation.png") &&
              empty(mcpPixels) && internal::imageDecodeRejectionsForTests() == before);
    }
    const auto path = root / "allocation.hdr";
    writeFile(path, encoded("hdr", 8, 8));
    Pixels pixels;
    internal::failImageDecodeAllocationAfterForTests(-1);
    check("float allocation fixture", bool(pixels.loadHDR(path)));
    const auto allocations = internal::imageDecodeAllocationAttemptsForTests();
    for (size_t step = 0; step < allocations; ++step) {
        internal::failImageDecodeAllocationAfterForTests(int64_t(step));
        check("file/read/float allocation failure is whole", pixels.loadHDR(path).error == LoadError::Unknown && empty(pixels));
    }
    internal::failImageDecodeAllocationAfterForTests(-1);
    internal::appRunToken() = make_shared<atomic<bool>>(true);
    Image image;
    int events = 0;
    const auto rejectedBefore = internal::imageDecodeRejectionsForTests();
    auto listener = image.onLoad.listen([&](LoadResult& result) {
        check("async allocation failure rejects whole image", result.error == LoadError::Unknown && !image.isAllocated());
        ++events;
    });
    internal::setResourceLoadProbeForTests([](const char* stage, const fs::path&) {
        if (string(stage) == "image.decode") internal::failImageDecodeAllocationAfterForTests(0);
    });
    image.loadAsync(path);
    internal::waitForResourceLoadsForTests();
    internal::setResourceLoadProbeForTests({});
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    check("async allocation failure completes without bound loss", events == 1 && !image.isLoading() &&
          internal::imageDecodeRejectionsForTests() == rejectedBefore);
    internal::appRunToken()->store(false);
}
} // namespace
#include "deviceFixtures.h"
TC_CORE_TEST_MAIN(int argc, char** argv) {
    bool hookRejected = false;
    failNew = true;
    try {
        void* block = ::operator new(sizeof(Pixels));
        ::operator delete(block);
    } catch (const std::bad_alloc&) { hookRejected = true; }
    failNew = false;
    check("global allocation-failure hook is active", hookRejected);
    if (argc > 1 && string(argv[1]) == "--callback-input") {
        callbackInputChecks();
        return failures ? 1 : 0;
    }
    if (argc > 1) return imageDeviceMain(argc, argv);
    const auto root = fs::temp_directory_path() / "tc_imageDecodeBounds";
    fs::create_directories(root);
    atomic<int> warnings{0}, errors{0};
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.message.find("[Pixels]") == string::npos) return;
        if (event.level == LogLevel::Warning) {
            ++warnings;
            check("bound Warning reports rejection", event.message.find("image rejected: ") != string::npos);
        }
        if (event.level == LogLevel::Error) ++errors;
    });
    writeFile(root / "corrupt.png", {1,2,3});
    Pixels corrupt;
    const auto before = internal::imageDecodeRejectionsForTests();
    check("corrupt file preserves first bound Warning", corrupt.load(root / "corrupt.png").error == LoadError::DecodeFailed &&
          warnings == 0 && internal::imageDecodeRejectionsForTests() == before);
    Pixels corruptMcp;
    check("corrupt MCP file counts no bound loss", !mcp::internal::loadMcpImage(corruptMcp, root / "corrupt.png") &&
          warnings == 0 && internal::imageDecodeRejectionsForTests() == before);
    earlyPngChecks(root);
    boundaryChecks(root);
    check("local MCP loading emits no axis admission Warning", warnings == 0);
    headerChecks();
    inputSizeChecks(root);
    callbackInputChecks();
    mipSamplingChecks();
    allocationChecks(root);
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    gpuReplacementChecks(root);
#endif
#ifdef __APPLE__
    platformChecks(root);
#endif
    check("one first-loss Warning across repeated and worker rejections", warnings == 1);
    check("allocation failures emit Errors", errors > 0);
    internal::recordImageDecodeLimit(0);
    fs::remove_all(root);
    return failures ? 1 : 0;
}
