// =============================================================================
// Android Sound implementation (stub)
// =============================================================================
// TODO: Implement AAC decoding using Android MediaCodec NDK API
// or OpenSLES / AAudio for playback.
// =============================================================================

#include "TrussC.h"

#ifdef __ANDROID__

#pragma message("STUB: tcSound_android.cpp — needs MediaCodec or FFmpeg AAC decoding")

namespace trussc {

bool SoundBuffer::loadAac(const std::string& path) {
    logWarning("SoundBuffer") << "AAC loading not yet implemented on Android";
    return false;
}

bool SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    logWarning("SoundBuffer") << "AAC loading from memory not yet implemented on Android";
    return false;
}

} // namespace trussc

#endif // __ANDROID__
