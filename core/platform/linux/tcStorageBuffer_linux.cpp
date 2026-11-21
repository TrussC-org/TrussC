#include "TrussC.h"
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
#include "../gl/tcStorageBufferReadback.h"
#else
namespace trussc {
bool StorageBuffer::readPlatform(void*, size_t) { return false; }
} // namespace trussc
#endif
#endif
