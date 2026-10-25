// =============================================================================
// tcSound_ios.mm - AAC decoding using AudioToolbox
// Shared with macOS version (pure AudioToolbox, no AppKit dependency)
// =============================================================================

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "tc/sound/internal/tcAudioToolboxDecode.h"
#include "tc/sound/tcMicInputInternal.h"

namespace trussc {
namespace internal {
MicAuthorization micAuthorizationPlatform() {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized: return MicAuthorization::Authorized;
        case AVAuthorizationStatusNotDetermined: return MicAuthorization::NotDetermined;
        case AVAuthorizationStatusRestricted: return MicAuthorization::Restricted;
        default: return MicAuthorization::Denied;
    }
}
void requestMicPermissionPlatform(std::function<void(bool)> completion) {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
        completion(granted);
    }];
}
} // namespace internal

// -----------------------------------------------------------------------------
// Memory read callbacks for AudioFile
// -----------------------------------------------------------------------------
struct MemoryAudioFileData {
    const uint8_t* data;
    size_t dataSize;
    size_t position;
};

static OSStatus MemoryAudioFile_ReadProc(void* inClientData,
                                          SInt64 inPosition,
                                          UInt32 requestCount,
                                          void* buffer,
                                          UInt32* actualCount) {
    MemoryAudioFileData* fileData = static_cast<MemoryAudioFileData*>(inClientData);

    if (inPosition < 0 || static_cast<size_t>(inPosition) >= fileData->dataSize) {
        *actualCount = 0;
        return noErr;
    }

    size_t bytesAvailable = fileData->dataSize - static_cast<size_t>(inPosition);
    size_t bytesToRead = (requestCount < bytesAvailable) ? requestCount : bytesAvailable;

    memcpy(buffer, fileData->data + inPosition, bytesToRead);
    *actualCount = static_cast<UInt32>(bytesToRead);

    return noErr;
}

static SInt64 MemoryAudioFile_GetSizeProc(void* inClientData) {
    MemoryAudioFileData* fileData = static_cast<MemoryAudioFileData*>(inClientData);
    return static_cast<SInt64>(fileData->dataSize);
}

namespace {
LoadResult decodeAacMemory(const void* data, size_t dataSize, SoundBuffer& out, internal::AudioDecodeBudget& budget) {
    if (!data || !dataSize || !budget.encoded(dataSize))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC input rejected; use SoundStream::loadStream()");
    MemoryAudioFileData memory{static_cast<const uint8_t*>(data), dataSize, 0};
    AudioFileID file = nullptr;
    const AudioFileTypeID types[] = {kAudioFileAAC_ADTSType, kAudioFileM4AType, 0};
    for (AudioFileTypeID type : types) {
        if (AudioFileOpenWithCallbacks(&memory, MemoryAudioFile_ReadProc, nullptr,
                MemoryAudioFile_GetSizeProc, nullptr, type, &file) == noErr && file) break;
        if (file) AudioFileClose(file);
        file = nullptr;
    }
    if (!file) {
        logError("SoundBuffer") << "failed to open AAC memory; previous PCM kept";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to open AAC memory");
    }
    auto result = internal::decodeAudioToolbox(file, out, budget);
    AudioFileClose(file);
    if (!result && !budget.rejected && !budget.allocationFailed)
        logError("SoundBuffer") << "AAC decoder failed; previous PCM kept";
    return result;
}
} // namespace

LoadResult SoundBuffer::loadAacPlatform(const fs::path& path) {
    internal::AudioDecodeBudget budget;
    std::vector<unsigned char> bytes;
    if (!internal::readAudioFile(path, bytes, budget))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC input rejected; use SoundStream::loadStream()");
    return decodeAacMemory(bytes.data(), bytes.size(), *this, budget);
}

LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    internal::AudioDecodeBudget budget;
    return decodeAacMemory(data, dataSize, *this, budget);
}

} // namespace trussc
