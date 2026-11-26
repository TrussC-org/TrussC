#include "TrussC.h"

#if defined(__EMSCRIPTEN__) && defined(SOKOL_GLES3)
#include "../gl/tcFboReadback.h"
#elif defined(__EMSCRIPTEN__)

namespace trussc {

bool Fbo::readPixelsPlatform(unsigned char* pixels, bool reportError) const {
    // WebGPU backend does not support synchronous pixel readback.
    // sokol creates render-target textures without CopySrc usage, and
    // wgpuBufferMapAsync requires ASYNCIFY yielding which conflicts with
    // the sokol frame lifecycle. This is a known limitation.
    // TODO: Implement async readback when sokol adds CopySrc support.
    if (reportError && readPixelsWarning_.isFirstTime()) logWarning("Fbo") << "readPixels is not supported on WebGPU (Emscripten)";
    return false;
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    if (readPixelsFloatWarning_.isFirstTime()) logWarning("Fbo") << "readPixelsFloat is not supported on WebGPU (Emscripten)";
    return false;
}

} // namespace trussc

#endif
