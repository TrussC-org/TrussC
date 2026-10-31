// =============================================================================
// tcPixels.cpp - Pixels implementation (requires TrussC.h for getDataPath)
// =============================================================================

#include "TrussC.h"
#include "impl/tcImageDecode.h"
#include <atomic>
#include <climits>
#include <new>
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif
#include "stb/stb_image_write.h"

namespace trussc {

namespace internal {
namespace {
std::atomic<int> imageDecodeLimit{0};
std::atomic<uint64_t> imageDecodeRejections{0};
thread_local ImageDecodeAllocationScope* imageDecodeAllocation = nullptr;
thread_local int64_t imageDecodeFailAfter = -1;
thread_local size_t imageDecodeAllocationAttempts = 0;
thread_local size_t imageDecodeLargestAllocation = 0;
constexpr size_t encodedImageLimit = 64 * 1024 * 1024;
}
void recordImageDecodeLimit(int axis) { imageDecodeLimit.store(axis > 0 ? axis : 0, std::memory_order_relaxed); }
int imageDecodeAxisLimit() {
    const int axis = imageDecodeLimit.load(std::memory_order_relaxed);
    return axis > 0 ? axis : 16384;
}
uint64_t imageDecodeRejectionsForTests() { return imageDecodeRejections.load(std::memory_order_relaxed); }
LoadResult rejectImageDecode(LoadError error, const char* message) {
    if (imageDecodeRejections.fetch_add(1, std::memory_order_relaxed) == 0)
        getLogger().log(LogLevel::Warning, std::string("[Pixels] image rejected: ") + message + "; further rejections are counted");
    return LoadResult::fail(error, message);
}
LoadResult imageDecodeOutOfMemory() noexcept {
    LoadResult result;
    result.error = LoadError::Unknown;
#ifdef __EMSCRIPTEN__
    // Keep reporting allocation-free on Web: Logger's timestamp can allocate.
    emscripten_console_error("[Pixels] image allocation failed");
#else
    try { result.message = "image allocation failed"; } catch (...) {}
    // Never use LogStream here: its noexcept destructor allocates.
    try { getLogger().log(LogLevel::Error, "[Pixels] image allocation failed"); } catch (...) {}
#endif
    return result;
}

LoadResult admitImageEncodedSize(size_t bytes) {
    // Bound (security): 64 MiB limits untrusted encoded input storage before decoding. Overflow rejects the whole image and is counted.
    if (bytes > encodedImageLimit)
        return rejectImageDecode(LoadError::TooLarge,
            ("encoded size " + std::to_string(bytes) + " exceeds 64 MiB").c_str());
    return LoadResult::success();
}
LoadResult admitImageDimensions(int64_t width, int64_t height, size_t channels, size_t elementBytes) {
    if (width <= 0 || height <= 0 || channels == 0 || elementBytes == 0)
        return LoadResult::fail(LoadError::DecodeFailed, "invalid image dimensions or format");
    // Bound (gpu): decoded axes must fit the recorded GPU texture limit (16384 when unknown). Overflow rejects the whole image and is counted.
    const int axis = imageDecodeAxisLimit();
    if (width > axis || height > axis)
        return rejectImageDecode(LoadError::TooLarge,
            (std::to_string(width) + "x" + std::to_string(height) +
             " exceeds the GPU limit of " + std::to_string(axis) + " per axis").c_str());
    // Bound (abi): all decoded pixel/channel/element products must fit size_t. Overflow rejects the whole image and is counted.
    const size_t available = std::numeric_limits<size_t>::max();
    if (uint64_t(width) > available / uint64_t(height) ||
        size_t(width) * size_t(height) > available / channels ||
        size_t(width) * size_t(height) * channels > available / elementBytes)
        return rejectImageDecode(LoadError::TooLarge,
            ("pixel store " + std::to_string(width) + "x" + std::to_string(height) +
             " x " + std::to_string(channels) + " channels x " + std::to_string(elementBytes) +
             " bytes exceeds size_t capacity " + std::to_string(available)).c_str());
    return LoadResult::success();
}
ImageDecodeAllocationScope::ImageDecodeAllocationScope() : previous_(imageDecodeAllocation) {
    imageDecodeAllocation = this;
}
ImageDecodeAllocationScope::~ImageDecodeAllocationScope() { imageDecodeAllocation = previous_; }
void failImageDecodeAllocationAfterForTests(int64_t count) {
    imageDecodeFailAfter = count;
    imageDecodeAllocationAttempts = 0;
    imageDecodeLargestAllocation = 0;
}
size_t imageDecodeAllocationAttemptsForTests() { return imageDecodeAllocationAttempts; }
size_t imageDecodeLargestAllocationForTests() { return imageDecodeLargestAllocation; }
bool imageDecodeAllocationAllowed(size_t bytes) {
    if (imageDecodeAllocation) imageDecodeLargestAllocation = std::max(imageDecodeLargestAllocation, bytes);
    if (!imageDecodeAllocation) return true;
    ++imageDecodeAllocationAttempts;
    if (imageDecodeFailAfter < 0) return true;
    if (imageDecodeFailAfter > 0) { --imageDecodeFailAfter; return true; }
    imageDecodeAllocation->failed_ = true;
    return false;
}
void imageDecodeAllocationFailed() {
    if (imageDecodeAllocation) imageDecodeAllocation->failed_ = true;
}
LoadResult readEncodedImage(const fs::path& path, std::unique_ptr<unsigned char[]>& bytes, size_t& length) {
    LoadResult entry;
    auto closeFile = [](FILE* handle) { std::fclose(handle); };
    std::unique_ptr<FILE, decltype(closeFile)> file(openLoadFile(path, "Pixels", entry), closeFile);
    if (!file) return entry;
    std::error_code ec;
    const auto fileLength = fs::file_size(path, ec);
    if (ec) return LoadResult::fail(LoadError::DecodeFailed, "cannot determine encoded image size");
    if (fileLength > SIZE_MAX)
        return LoadResult::fail(LoadError::TooLarge, "encoded image length exceeds size_t capacity");
    if (!fileLength) return LoadResult::fail(LoadError::DecodeFailed, "empty encoded image");
    length = size_t(fileLength);
    ImageDecodeAllocationScope allocation;
    try {
        // One immutable snapshot also prevents a path change between
        // metadata inspection and decoding (including the ImageIO fallback).
        if (imageDecodeAllocationAllowed()) bytes.reset(new (std::nothrow) unsigned char[length]);
        else return imageDecodeOutOfMemory();
        if (!bytes && length) return imageDecodeOutOfMemory();
        if (std::fread(bytes.get(), 1, length, file.get()) != length ||
            std::ferror(file.get()))
            return LoadResult::fail(LoadError::DecodeFailed, "cannot read encoded image");
        if (std::fgetc(file.get()) != EOF)
            return LoadResult::fail(LoadError::DecodeFailed, "encoded image changed while reading");
        return LoadResult::success();
    } catch (const std::bad_alloc&) { return imageDecodeOutOfMemory(); }
}
void* ImageCallbackInput::decode(bool asFloat, int& width, int& height, int& channels) {
    const stbi_io_callbacks callbacks{
        [](void* context, char* output, int requested) {
            auto& input = *static_cast<ImageCallbackInput*>(context);
            // Bound (representation): stb counts input with a 32-bit int; reserve its 128-byte refill buffer.
            const size_t remaining = size_t(INT_MAX) - 128 - input.delivered;
            const size_t available = std::min(size_t(requested), input.length - input.position);
            const size_t count = std::min(available, remaining);
            if (available > remaining) input.limitReached = true;
            input.copyAt(input.source, input.position, output, count);
            input.position += count;
            input.delivered += count;
            return int(count);
        },
        [](void* context, int distance) {
            auto& input = *static_cast<ImageCallbackInput*>(context);
            if (distance < 0) input.position -= std::min(input.position, size_t(-int64_t(distance)));
            else input.position += std::min(input.length - input.position, size_t(distance));
        },
        [](void* context) {
            auto& input = *static_cast<ImageCallbackInput*>(context);
            if (input.delivered == size_t(INT_MAX) - 128 && input.position < input.length)
                input.limitReached = true;
            return int(input.limitReached || input.position == input.length);
        }
    };
    return asFloat ? static_cast<void*>(stbi_loadf_from_callbacks(&callbacks, this, &width, &height, &channels, 3)) :
                     static_cast<void*>(stbi_load_from_callbacks(&callbacks, this, &width, &height, &channels, 4));
}
LoadResult ImageCallbackInput::result(const fs::path& path) const {
    if (!limitReached) return LoadResult::success();
    const auto message = "image input over 2 GiB: stb_image counts input with a 32-bit int: " + pathToDisplayUtf8(path);
    getLogger().log(LogLevel::Error, "[Pixels] " + message);
    return LoadResult::fail(LoadError::TooLarge, message);
}
} // namespace internal

LoadResult Pixels::loadEncoded(const unsigned char* buffer, size_t len, bool asFloat, const fs::path& path) {
    if (!buffer || len == 0)
        return LoadResult::fail(LoadError::DecodeFailed, "invalid encoded image data");
    internal::ImageDecodeAllocationScope allocation;
    int decodedW = 0, decodedH = 0, decodedChannels = 0;
    using Decoded = std::unique_ptr<void, decltype(&stbi_image_free)>;
    Decoded decoded(nullptr, &stbi_image_free);
    if (len <= INT_MAX) {
        decoded.reset(asFloat ? static_cast<void*>(stbi_loadf_from_memory(buffer, int(len), &decodedW, &decodedH, &decodedChannels, 3)) :
                                static_cast<void*>(stbi_load_from_memory(buffer, int(len), &decodedW, &decodedH, &decodedChannels, 4)));
    } else {
        internal::ImageCallbackInput input{buffer, len, [](const void* data, size_t position, char* output, size_t count) {
            std::memcpy(output, static_cast<const unsigned char*>(data) + position, count);
        }};
        decoded.reset(input.decode(asFloat, decodedW, decodedH, decodedChannels));
        auto result = input.result(path);
        if (!result) return result;
    }
    if (allocation.failed()) { decoded.reset(); return internal::imageDecodeOutOfMemory(); }
    if (!decoded) return LoadResult::fail(LoadError::DecodeFailed, stbi_failure_reason() ? stbi_failure_reason() : "decoder rejected image data");
    const int w = decodedW, h = decodedH;
    size_t elements = 0;
    if (!internal::pixelBufferCount(w, h, 4, asFloat ? sizeof(float) : 1, SIZE_MAX, elements))
        return LoadResult::fail(LoadError::TooLarge, "decoded pixel storage exceeds size_t capacity");
    try {
        // Allocate first; publish dimensions and storage only after a full copy.
        const size_t count = elements / 4;
        if (asFloat) {
            std::unique_ptr<float[]> output(internal::imageDecodeAllocationAllowed()
                ? new (std::nothrow) float[count * 4] : nullptr);
            if (!output) { decoded.reset(); return internal::imageDecodeOutOfMemory(); }
            const auto* source = static_cast<const float*>(decoded.get());
            for (size_t i = 0; i < count; ++i) {
                for (size_t c = 0; c < 3; ++c) output[i * 4 + c] = source[i * 3 + c];
                output[i * 4 + 3] = 1.0f;
            }
            data_ = output.release();
        } else {
            std::unique_ptr<unsigned char[]> output(internal::imageDecodeAllocationAllowed()
                ? new (std::nothrow) unsigned char[count * 4] : nullptr);
            if (!output) { decoded.reset(); return internal::imageDecodeOutOfMemory(); }
            std::memcpy(output.get(), decoded.get(), count * 4);
            data_ = output.release();
        }
        width_ = w;
        height_ = h;
        channels_ = 4;
        format_ = asFloat ? PixelFormat::F32 : PixelFormat::U8;
        allocated_ = true;
        return LoadResult::success();
    } catch (const std::bad_alloc&) { decoded.reset(); return internal::imageDecodeOutOfMemory(); }
}
LoadResult Pixels::loadFile(const fs::path& filePath, bool asFloat) {
    clear();
    try {
        const auto path = internal::resolveLoadPath(filePath);
        std::unique_ptr<unsigned char[]> bytes;
        size_t length = 0;
        // Bulk decoder I/O can also report allocation failure through a null
        // result; this host TU enables exception cleanup for validation too.
        auto result = internal::readEncodedImage(path, bytes, length);
        if (!result) return result;
        result = loadEncoded(bytes.get(), length, asFloat, path);
#ifdef __APPLE__
        if (!result && result.error == LoadError::DecodeFailed && !asFloat)
            result = internal::decodePlatformImage(*this, bytes.get(), length);
#endif
        return result;
    } catch (const std::bad_alloc&) {
        return internal::imageDecodeOutOfMemory();
    }
}
LoadResult Pixels::load(const fs::path& path) { return loadFile(path, false); }
LoadResult Pixels::loadHDR(const fs::path& path) { return loadFile(path, true); }
LoadResult Pixels::loadFromMemory(const unsigned char* buffer, int len) try {
    clear();
    if (!buffer || len <= 0)
        return LoadResult::fail(LoadError::DecodeFailed, "invalid encoded image data");
    auto admitted = internal::admitImageEncodedSize(size_t(len));
    if (!admitted) return admitted;
    auto result = loadEncoded(buffer, len, false);
    return result;
} catch (const std::bad_alloc&) {
    clear();
    return internal::imageDecodeOutOfMemory();
}

static SaveResult imageEncodeCallbackFailure(bool report) noexcept {
    SaveResult failure{SaveError::WriteFailed, {}};
    try {
        failure.message = "Image encoding callback failed";
        if (report) return internal::reportSaveFailure(std::move(failure), "Pixels");
        return failure;
    } catch (...) { return SaveResult{SaveError::WriteFailed, {}}; }
}

SaveResult internal::writePixelsEncoded(std::FILE* file, const Pixels& pixels,
                                        const std::string& ext, const fs::path& path) {
    struct Sink { std::FILE* file; const fs::path& path; SaveResult result; bool failed = false; } sink{file, path, {}};
    auto write = [](void* context, void* data, int size) noexcept {
        auto& sink = *static_cast<Sink*>(context);
        if (sink.failed || !sink.result) return;
        try { sink.result = internal::checkedWriteUnflushed(sink.file, data, size, sink.path); }
        catch (...) { sink.failed = true; }
    };
    const int w = pixels.getWidth(), h = pixels.getHeight(), ch = pixels.getChannels();
    const auto* data = pixels.getData();
    int encoded;
    if (ext == "jpg" || ext == "jpeg") encoded = stbi_write_jpg_to_func(write, &sink, w, h, ch, data, 90);
    else if (ext == "bmp") encoded = stbi_write_bmp_to_func(write, &sink, w, h, ch, data);
    else if (ext == "tga") encoded = stbi_write_tga_to_func(write, &sink, w, h, ch, data);
    else encoded = stbi_write_png_to_func(write, &sink, w, h, ch, data, w * ch);
    if (sink.failed) return imageEncodeCallbackFailure(false);
    if (!sink.result) return sink.result;
    return encoded ? SaveResult::success() : internal::saveFailure(path, "Screenshot encoding failed");
}

SaveResult Pixels::save(const fs::path& path) const {
    if (!allocated_ || !data_ || format_ == PixelFormat::F32)
        return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed,
            "Cannot save empty or floating-point pixels"), "Pixels");
    fs::path savePath;
    auto prepared = internal::prepareOutputPath(path, savePath, "Pixels");
    if (!prepared) return prepared;

    // Encode to memory first: an encode error leaves an existing file
    // untouched. The encoder follows the extension, whatever its case
    // (.PNG, .Jpg); the file is written under the path as given.
    auto ext = toLower(getFileExtension(savePath));
    struct Encoded { std::vector<unsigned char> bytes; bool failed = false; } encoded;
    auto append = [](void* ctx, void* d, int size) noexcept {
        auto& out = *static_cast<Encoded*>(ctx);
        if (out.failed) return;
        auto* bytes = static_cast<unsigned char*>(d);
        try { out.bytes.insert(out.bytes.end(), bytes, bytes + size); }
        catch (...) { out.failed = true; }
    };
    int result = 0;
    if (ext == "jpg" || ext == "jpeg") {
        result = stbi_write_jpg_to_func(append, &encoded, width_, height_, channels_, data_, 90);
    } else if (ext == "bmp") {
        result = stbi_write_bmp_to_func(append, &encoded, width_, height_, channels_, data_);
    } else {
        // Default is PNG
        result = stbi_write_png_to_func(append, &encoded, width_, height_, channels_, data_, width_ * channels_);
    }
    // stb's void callback cannot cancel encoding. Discard partial bytes and
    // report only after it returns, without unwinding through its C linkage.
    if (encoded.failed) return imageEncodeCallbackFailure(true);
    if (result == 0 || encoded.bytes.empty())
        return internal::reportSaveFailure(internal::saveFailure(savePath, "Cannot encode image"), "Pixels");
    return internal::atomicWrite(savePath, encoded.bytes.data(), encoded.bytes.size(), "Pixels");
}

// Platform-specific image loader fallback (non-Apple stub)
#ifndef __APPLE__
bool Pixels::loadPlatform(const fs::path& path) {
    if (!internal::checkLoadFile(internal::resolveLoadPath(path), "Pixels")) return false;
    return false;
}
#endif

// =============================================================================
// Image operations: halve / resize / crop / mirror
// =============================================================================
//
// Math is done in linear light for U8 buffers. The convention used by the
// gamma helpers (sRGBToLinear / linearToSRGB above in the header) is gamma
// 2.2 — same as the FBO mipmap downsample shader and the rest of the
// gamma-correct paths. F32 buffers are assumed to already be linear (HDR
// sources, intermediate compute), so no conversion is applied.
//
// "isLinearChannel_(c)" decides whether channel `c` needs sRGB
// decode/encode: only R/G/B of a 3- or 4-channel buffer go through the
// curve; alpha and single-/two-channel buffers (masks, normals, etc.)
// stay linear.

namespace {

// Catmull-Rom bicubic kernel weight at signed distance d. The result is
// undefined outside [-2, 2]; callers are expected to only sample within
// that window. Sum of weights over -1, 0, 1, 2 is exactly 1, so the
// caller can skip normalisation.
inline float catmullRomWeight(float d) {
    float a = std::abs(d);
    if (a <= 1.0f) return ((1.5f * a - 2.5f) * a) * a + 1.0f;
    if (a <  2.0f) return (((-0.5f * a) + 2.5f) * a - 4.0f) * a + 2.0f;
    return 0.0f;
}

// sRGB <-> linear conversion (gamma 2.2 approximation), same convention
// as the rest of the gamma-correct pipeline.
inline float sRGBToLinear(float s) { return std::pow(s, 2.2f); }
inline float linearToSRGB(float l) { return std::pow(l, 1.0f / 2.2f); }

// For 3- and 4-channel buffers, the R/G/B channels are sRGB-encoded;
// alpha (channel 3 in RGBA) and anything in a single-/two-channel buffer
// (masks, normals, etc.) stays linear.
inline bool isLinearChannel(int channels, int c) {
    if (channels <= 2) return true;
    return c == 3;
}

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // anonymous namespace

void Pixels::halve() {
    if (!allocated_) return;
    int newW = std::max(width_ / 2, 1);
    int newH = std::max(height_ / 2, 1);

    Pixels dst;
    dst.allocate(newW, newH, channels_, format_);

    halveInto(dst);
    *this = std::move(dst);
}

// Both normal transforms and image-load mips use the same sampling math.
void Pixels::halveInto(Pixels& dst) const {
    const int newW = dst.width_, newH = dst.height_;
    const bool isF32 = (format_ == PixelFormat::F32);
    const int   ch    = channels_;
    const int   sw    = width_;
    const int   sh    = height_;

    auto srcF = static_cast<const float*>(data_);
    auto srcU = static_cast<const unsigned char*>(data_);
    auto dstF = static_cast<float*>(dst.data_);
    auto dstU = static_cast<unsigned char*>(dst.data_);

    for (int y = 0; y < newH; y++) {
        int sy0 = std::min(2 * y,     sh - 1);
        int sy1 = std::min(2 * y + 1, sh - 1);
        for (int x = 0; x < newW; x++) {
            int sx0 = std::min(2 * x,     sw - 1);
            int sx1 = std::min(2 * x + 1, sw - 1);
            for (int c = 0; c < ch; c++) {
                const size_t i00 = internal::pixelOffset(sx0, sy0, sw, ch) + c;
                const size_t i01 = internal::pixelOffset(sx1, sy0, sw, ch) + c;
                const size_t i10 = internal::pixelOffset(sx0, sy1, sw, ch) + c;
                const size_t i11 = internal::pixelOffset(sx1, sy1, sw, ch) + c;
                const size_t di  = internal::pixelOffset(x, y, newW, ch) + c;
                if (isF32) {
                    // F32 = already linear, average directly.
                    float avg = (srcF[i00] + srcF[i01] + srcF[i10] + srcF[i11]) * 0.25f;
                    dstF[di] = avg;
                } else {
                    bool linear = isLinearChannel(ch, c);
                    auto fetch = [&](size_t i) -> float {
                        float v = srcU[i] / 255.0f;
                        return linear ? v : sRGBToLinear(v);
                    };
                    float avg = (fetch(i00) + fetch(i01) + fetch(i10) + fetch(i11)) * 0.25f;
                    if (!linear) avg = linearToSRGB(avg);
                    avg = clampf(avg, 0.0f, 1.0f);
                    dstU[di] = static_cast<unsigned char>(std::lround(avg * 255.0f));
                }
            }
        }
    }

}

bool internal::ImageLoadAccess::allocate(Pixels& pixels, int width, int height, bool asFloat) {
    size_t count = 0;
    if (!pixelBufferCount(width, height, 4, asFloat ? sizeof(float) : 1, SIZE_MAX, count)) return false;
    void* data = nullptr;
    if (imageDecodeAllocationAllowed(count * (asFloat ? sizeof(float) : 1))) {
        if (asFloat) data = new (std::nothrow) float[count];
        else data = new (std::nothrow) unsigned char[count];
    }
    if (!data) { imageDecodeAllocationFailed(); return false; }
    pixels.clear();
    pixels.data_ = data;
    pixels.width_ = width;
    pixels.height_ = height;
    pixels.channels_ = 4;
    pixels.format_ = asFloat ? PixelFormat::F32 : PixelFormat::U8;
    pixels.allocated_ = true;
    return true;
}

bool internal::ImageLoadAccess::mipLevel(Pixels& destination, const Pixels& source) {
    if (!allocate(destination, std::max(source.width_ / 2, 1),
                  std::max(source.height_ / 2, 1), source.isFloat())) return false;
    source.halveInto(destination);
    return true;
}

void Pixels::crop(int x, int y, int w, int h) {
    if (!allocated_ || w <= 0 || h <= 0) return;

    Pixels dst;
    dst.allocate(w, h, channels_, format_);
    if (!dst.isAllocated()) return;

    const size_t bpp = (format_ == PixelFormat::F32)
                       ? (size_t)channels_ * sizeof(float)
                       : (size_t)channels_;
    auto srcBytes = static_cast<const unsigned char*>(data_);
    auto dstBytes = static_cast<unsigned char*>(dst.data_);

    const int sw = width_;
    const int sh = height_;

    for (int dy = 0; dy < h; dy++) {
        // clamp-to-edge: rows outside the source repeat the nearest in-bounds row.
        int sy = y + dy;
        if (sy < 0) sy = 0;
        else if (sy >= sh) sy = sh - 1;
        for (int dx = 0; dx < w; dx++) {
            int sx = x + dx;
            if (sx < 0) sx = 0;
            else if (sx >= sw) sx = sw - 1;
            std::memcpy(dstBytes + ((size_t)dy * w + dx) * bpp,
                        srcBytes + ((size_t)sy * sw + sx) * bpp,
                        bpp);
        }
    }

    *this = std::move(dst);
}

void Pixels::mirror(bool horizontal, bool vertical) {
    if (!allocated_ || (!horizontal && !vertical)) return;

    const size_t bpp = (format_ == PixelFormat::F32)
                       ? (size_t)channels_ * sizeof(float)
                       : (size_t)channels_;
    auto bytes = static_cast<unsigned char*>(data_);
    std::vector<unsigned char> tmp(bpp);

    if (horizontal) {
        for (int yy = 0; yy < height_; yy++) {
            unsigned char* row = bytes + (size_t)yy * width_ * bpp;
            for (int a = 0, b = width_ - 1; a < b; a++, b--) {
                std::memcpy(tmp.data(),   row + (size_t)a * bpp, bpp);
                std::memcpy(row + (size_t)a * bpp, row + (size_t)b * bpp, bpp);
                std::memcpy(row + (size_t)b * bpp, tmp.data(),   bpp);
            }
        }
    }
    if (vertical) {
        const size_t rowBytes = (size_t)width_ * bpp;
        std::vector<unsigned char> rowTmp(rowBytes);
        for (int a = 0, b = height_ - 1; a < b; a++, b--) {
            std::memcpy(rowTmp.data(),                    bytes + (size_t)a * rowBytes, rowBytes);
            std::memcpy(bytes + (size_t)a * rowBytes,     bytes + (size_t)b * rowBytes, rowBytes);
            std::memcpy(bytes + (size_t)b * rowBytes,     rowTmp.data(),                rowBytes);
        }
    }
}

// One-axis resample helper for Pixels::resize.
//
// Resamples `src` along the X axis only into `dst`. Both buffers must
// match in height, channels and format; only the widths differ. Picks
// BoxArea when downscaling (`dstW < srcW`) and Catmull-Rom bicubic when
// upscaling. For U8 buffers, RGB channels run through the gamma 2.2
// curve so the average lives in linear light; alpha and non-RGB buffers
// stay linear.
static void resample1D_X(const Pixels& src, Pixels& dst) {
    const int sw = src.getWidth();
    const int sh = src.getHeight();
    const int dw = dst.getWidth();
    const int ch = src.getChannels();
    const bool isF32 = src.isFloat();
    const bool downscale = dw < sw;
    const float scale = (float)sw / (float)dw;  // > 1 downscale, < 1 upscale

    const float* srcF = isF32 ? static_cast<const float*>(src.getDataVoid()) : nullptr;
    const unsigned char* srcU = isF32 ? nullptr : static_cast<const unsigned char*>(src.getDataVoid());
    float* dstF = isF32 ? static_cast<float*>(dst.getDataVoid()) : nullptr;
    unsigned char* dstU = isF32 ? nullptr : static_cast<unsigned char*>(dst.getDataVoid());

    auto fetchLinear = [&](int y, int x, int c, bool isLinearCh) -> float {
        if (isF32) return srcF[((size_t)y * sw + x) * ch + c];
        float v = srcU[((size_t)y * sw + x) * ch + c] / 255.0f;
        return isLinearCh ? v : sRGBToLinear(v);
    };
    auto storeLinear = [&](int y, int x, int c, bool isLinearCh, float v) {
        if (isF32) {
            dstF[((size_t)y * dw + x) * ch + c] = v;
            return;
        }
        if (!isLinearCh) v = linearToSRGB(v);
        if (v < 0.0f) v = 0.0f;
        else if (v > 1.0f) v = 1.0f;
        dstU[((size_t)y * dw + x) * ch + c] = static_cast<unsigned char>(std::lround(v * 255.0f));
    };

    auto isLinearChannel = [&](int c) -> bool {
        if (ch <= 2) return true;
        return c == 3;  // alpha
    };

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < dw; x++) {
            for (int c = 0; c < ch; c++) {
                bool linCh = isLinearChannel(c);
                float v = 0.0f;
                if (downscale) {
                    // BoxArea — every source texel inside [startF, endF) contributes
                    // proportional to its overlap with that interval.
                    float startF = x * scale;
                    float endF   = (x + 1) * scale;
                    int startI = (int)std::floor(startF);
                    int endI   = std::min((int)std::ceil(endF), sw);
                    float sum = 0.0f, total = 0.0f;
                    for (int si = startI; si < endI; si++) {
                        float w = std::min(endF, (float)(si + 1)) - std::max(startF, (float)si);
                        if (w <= 0.0f) continue;
                        sum   += fetchLinear(y, si, c, linCh) * w;
                        total += w;
                    }
                    v = (total > 0.0f) ? (sum / total) : fetchLinear(y, std::min(startI, sw - 1), c, linCh);
                } else {
                    // Catmull-Rom bicubic — 4 neighbours of the sub-pixel position
                    // in source space, clamped to source bounds.
                    float center = (x + 0.5f) * scale - 0.5f;
                    int cf = (int)std::floor(center);
                    float t = center - (float)cf;
                    float sum = 0.0f;
                    for (int k = -1; k <= 2; k++) {
                        int si = cf + k;
                        if (si < 0) si = 0;
                        else if (si >= sw) si = sw - 1;
                        sum += fetchLinear(y, si, c, linCh) * catmullRomWeight((float)k - t);
                    }
                    v = sum;
                }
                storeLinear(y, x, c, linCh, v);
            }
        }
    }
}

// One-axis resample helper for Pixels::resize.
//
// Resamples `src` along the Y axis only into `dst`. Mirrors `resample1D_X`;
// only width is shared between buffers, heights differ.
static void resample1D_Y(const Pixels& src, Pixels& dst) {
    const int sw = src.getWidth();
    const int sh = src.getHeight();
    const int dh = dst.getHeight();
    const int ch = src.getChannels();
    const bool isF32 = src.isFloat();
    const bool downscale = dh < sh;
    const float scale = (float)sh / (float)dh;

    const float* srcF = isF32 ? static_cast<const float*>(src.getDataVoid()) : nullptr;
    const unsigned char* srcU = isF32 ? nullptr : static_cast<const unsigned char*>(src.getDataVoid());
    float* dstF = isF32 ? static_cast<float*>(dst.getDataVoid()) : nullptr;
    unsigned char* dstU = isF32 ? nullptr : static_cast<unsigned char*>(dst.getDataVoid());

    auto fetchLinear = [&](int y, int x, int c, bool isLinearCh) -> float {
        if (isF32) return srcF[((size_t)y * sw + x) * ch + c];
        float v = srcU[((size_t)y * sw + x) * ch + c] / 255.0f;
        return isLinearCh ? v : sRGBToLinear(v);
    };
    auto storeLinear = [&](int y, int x, int c, bool isLinearCh, float v) {
        if (isF32) {
            dstF[((size_t)y * sw + x) * ch + c] = v;
            return;
        }
        if (!isLinearCh) v = linearToSRGB(v);
        if (v < 0.0f) v = 0.0f;
        else if (v > 1.0f) v = 1.0f;
        dstU[((size_t)y * sw + x) * ch + c] = static_cast<unsigned char>(std::lround(v * 255.0f));
    };

    auto isLinearChannel = [&](int c) -> bool {
        if (ch <= 2) return true;
        return c == 3;
    };

    for (int y = 0; y < dh; y++) {
        for (int c = 0; c < ch; c++) {
            // weights / indices for this destination row, reused across all x.
            int wIndex[4];
            float wValue[4];
            int wCount = 0;
            float startF = 0.0f, endF = 0.0f;

            if (downscale) {
                startF = y * scale;
                endF   = (y + 1) * scale;
            } else {
                float center = (y + 0.5f) * scale - 0.5f;
                int cf = (int)std::floor(center);
                float t = center - (float)cf;
                for (int k = -1; k <= 2; k++) {
                    int si = cf + k;
                    if (si < 0) si = 0;
                    else if (si >= sh) si = sh - 1;
                    wIndex[wCount] = si;
                    wValue[wCount] = catmullRomWeight((float)k - t);
                    wCount++;
                }
            }

            for (int x = 0; x < sw; x++) {
                bool linCh = isLinearChannel(c);
                float v = 0.0f;
                if (downscale) {
                    int startI = (int)std::floor(startF);
                    int endI   = std::min((int)std::ceil(endF), sh);
                    float sum = 0.0f, total = 0.0f;
                    for (int si = startI; si < endI; si++) {
                        float w = std::min(endF, (float)(si + 1)) - std::max(startF, (float)si);
                        if (w <= 0.0f) continue;
                        sum   += fetchLinear(si, x, c, linCh) * w;
                        total += w;
                    }
                    v = (total > 0.0f) ? (sum / total) : fetchLinear(std::min(startI, sh - 1), x, c, linCh);
                } else {
                    float sum = 0.0f;
                    for (int k = 0; k < wCount; k++) {
                        sum += fetchLinear(wIndex[k], x, c, linCh) * wValue[k];
                    }
                    v = sum;
                }
                storeLinear(y, x, c, linCh, v);
            }
        }
    }
}

void Pixels::resize(int newW, int newH) {
    if (!allocated_ || newW <= 0 || newH <= 0) return;
    if (newW == width_ && newH == height_) return;

    // 2-pass separable: horizontal first into an intermediate of size
    // (newW x height_), then vertical into the final (newW x newH).
    Pixels intermediate;
    intermediate.allocate(newW, height_, channels_, format_);
    if (!intermediate.isAllocated()) return;
    resample1D_X(*this, intermediate);

    Pixels final;
    final.allocate(newW, newH, channels_, format_);
    if (!final.isAllocated()) return;
    resample1D_Y(intermediate, final);

    *this = std::move(final);
}

} // namespace trussc
