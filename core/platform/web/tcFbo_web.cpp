#include "TrussC.h"

#ifdef __EMSCRIPTEN__

namespace trussc {

bool Fbo::readPixelsPlatform(unsigned char* pixels) const {
    // WebGPU backend does not support synchronous pixel readback.
    // sokol creates render-target textures without CopySrc usage, and
    // wgpuBufferMapAsync requires ASYNCIFY yielding which conflicts with
    // the sokol frame lifecycle. This is a known limitation.
    // TODO: Implement async readback when sokol adds CopySrc support.
    logWarning("Fbo") << "readPixels is not supported on WebGPU (Emscripten)";
    return false;
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    logWarning("Fbo") << "readPixelsFloat is not supported on WebGPU (Emscripten)";
    return false;
}

} // namespace trussc

#endif
