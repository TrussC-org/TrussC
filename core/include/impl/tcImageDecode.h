#pragma once
#include "tc/utils/tcLoadResult.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace trussc { class Pixels; }

namespace trussc::internal {
// Only setup/shutdown writes this; loaders never query sokol from workers.
void recordImageDecodeLimit(int axis);
int imageDecodeAxisLimit(); // 16384 when no GPU limit has been recorded
uint64_t imageDecodeRejectionsForTests(); // cumulative, process lifetime
LoadResult rejectImageDecode(LoadError error, const char* message);
LoadResult imageDecodeOutOfMemory() noexcept;
// MCP-only dimension policy; CPU Pixels decoding has no axis cap.
LoadResult admitImageDimensions(int64_t width, int64_t height,
                                size_t channels, size_t elementBytes);
LoadResult readEncodedImage(const std::filesystem::path& path, std::unique_ptr<unsigned char[]>& bytes, size_t& length);

// Local snapshot adapter; copyAt also permits generated input in seam tests.
struct ImageCallbackInput {
    const void* source;
    size_t length;
    void (*copyAt)(const void*, size_t, char*, size_t);
    size_t position = 0, delivered = 0;
    bool limitReached = false;
    void* decode(bool asFloat, int& width, int& height, int& channels);
    LoadResult result(const std::filesystem::path& path) const;
};

// Observe allocation failure without imposing a separate working-memory cap.
class ImageDecodeAllocationScope {
public:
    ImageDecodeAllocationScope();
    ~ImageDecodeAllocationScope();
    bool failed() const { return failed_; }
    ImageDecodeAllocationScope(const ImageDecodeAllocationScope&) = delete;
    ImageDecodeAllocationScope& operator=(const ImageDecodeAllocationScope&) = delete;
private:
    friend bool imageDecodeAllocationAllowed(size_t bytes);
    friend void imageDecodeAllocationFailed();
    ImageDecodeAllocationScope* previous_;
    bool failed_ = false;
};
bool imageDecodeAllocationAllowed(size_t bytes = 0);
void imageDecodeAllocationFailed();
// Tests retained image decode failure reporting and MCP workspace admission.
// Thread-local deterministic failure injection; -1 disables it.
void failImageDecodeAllocationAfterForTests(int64_t successfulAllocations);
size_t imageDecodeAllocationAttemptsForTests();
size_t imageDecodeLargestAllocationForTests();

// Private access for image-derived storage; no throwing bulk allocations.
struct ImageLoadAccess {
    static bool allocate(Pixels& pixels, int width, int height, bool asFloat = false);
    static bool mipLevel(Pixels& destination, const Pixels& source);
};

#ifdef __APPLE__
LoadResult decodePlatformImage(Pixels& pixels, const unsigned char* bytes, size_t length);
#endif
}
