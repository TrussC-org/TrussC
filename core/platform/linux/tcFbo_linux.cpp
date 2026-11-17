#include "TrussC.h"
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
#include "../gl/tcFboReadback.h"
#else
namespace trussc {
bool Fbo::readPixelsPlatform(unsigned char*, bool) const {
    logWarning("Fbo") << "readPixels is not supported on the Vulkan backend yet";
    return false;
}
bool Fbo::readPixelsFloatPlatform(float*) const {
    logWarning("Fbo") << "readPixelsFloat is not supported on the Vulkan backend yet";
    return false;
}
} // namespace trussc
#endif
#endif
