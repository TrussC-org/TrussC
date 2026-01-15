// =============================================================================
// tcSound_win.cpp - AAC decoding stub (not yet implemented)
// =============================================================================

#include "tc/sound/tcSound.h"

namespace trussc {

bool SoundBuffer::loadAac(const std::string& path) {
    // TODO: Implement using Media Foundation
    printf("SoundBuffer: AAC decoding not yet implemented on Windows\n");
    return false;
}

bool SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    // TODO: Implement using Media Foundation
    printf("SoundBuffer: AAC decoding not yet implemented on Windows\n");
    return false;
}

} // namespace trussc
