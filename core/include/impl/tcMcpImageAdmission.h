#pragma once
#include <cstddef>
#include <cstdint>

namespace trussc { class Pixels; }

namespace trussc::mcp::internal {
// Checked storage and conservative codec workspace, before any pixel decode.
size_t reserveImageDecode(int width, int height, size_t sourceBytesPerPixel = 4);
#ifdef __APPLE__
bool loadMcpPlatformImage(Pixels& pixels, const unsigned char* encoded, size_t length);
#endif

void* mcpImageMalloc(size_t size);
void* mcpImageRealloc(void* pointer, size_t size);
void mcpImageFree(void* pointer);

// Synchronous codec scope: returned stb buffers must be freed before it ends.
// Outside a scope, stb retains its ordinary malloc/realloc/free layout.
class ImageAllocationScope {
public:
    explicit ImageAllocationScope(size_t limit);
    ~ImageAllocationScope();
    ImageAllocationScope(const ImageAllocationScope&) = delete;
    ImageAllocationScope& operator=(const ImageAllocationScope&) = delete;
    bool exceeded() const { return needed_ != 0; }
    size_t needed() const { return needed_; }
    size_t peak() const { return peak_; }
private:
    friend void* mcpImageMalloc(size_t);
    friend void* mcpImageRealloc(void*, size_t);
    friend void mcpImageFree(void*);
    ImageAllocationScope* previous_;
    void* first_ = nullptr;
    size_t limit_, used_ = 0, peak_ = 0, needed_ = 0;
};
}
