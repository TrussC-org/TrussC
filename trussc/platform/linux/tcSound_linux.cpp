// =============================================================================
// tcSound_linux.cpp - AAC decoding stub (not yet implemented)
// =============================================================================

#include "tc/sound/tcSound.h"

namespace trussc {

bool SoundBuffer::loadAac(const std::string& path) {
    // TODO: Implement using GStreamer or similar
    printf("SoundBuffer: AAC decoding not supported on Linux\n");
    return false;
}

bool SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    // TODO: Implement using GStreamer or similar
    printf("SoundBuffer: AAC decoding not supported on Linux\n");
    return false;
}

} // namespace trussc
