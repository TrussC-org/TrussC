// =============================================================================
// tcSound_mac.mm - AAC decoding using AudioToolbox
// =============================================================================

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include "tc/sound/tcSound.h"

namespace trussc {

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

// -----------------------------------------------------------------------------
// SoundBuffer::loadAac - macOS implementation (file-based)
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAac(const fs::path& path) {
    // Convert path to URL
    std::string pathStr = internal::pathToUtf8(path);

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return LoadResult::fail(LoadError::FileNotFound,
                                "file not found: " + pathStr);
    }

    NSString* nsPath = [NSString stringWithUTF8String:pathStr.c_str()];
    NSURL* fileURL = [NSURL fileURLWithPath:nsPath];

    if (!fileURL) {
        logError("SoundBuffer") << "invalid path: " << pathStr;
        return LoadResult::fail(LoadError::Unknown,
                                "invalid path: " + pathStr);
    }

    // Open audio file
    ExtAudioFileRef extAudioFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL((__bridge CFURLRef)fileURL, &extAudioFile);

    if (status != noErr || !extAudioFile) {
        logError("SoundBuffer") << "failed to open AAC file " << pathStr << " (status=" << (int)status << ")";
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to open AAC file: " + pathStr +
                                " (status=" + std::to_string((int)status) + ")");
    }

    // Get source format
    AudioStreamBasicDescription srcFormat;
    UInt32 propSize = sizeof(srcFormat);
    status = ExtAudioFileGetProperty(extAudioFile,
                                     kExtAudioFileProperty_FileDataFormat,
                                     &propSize,
                                     &srcFormat);
    if (status != noErr) {
        logError("SoundBuffer") << "failed to get the AAC format";
        ExtAudioFileDispose(extAudioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to get AAC format (status=" +
                                std::to_string((int)status) + ")");
    }

    // Set output format (32-bit float PCM)
    AudioStreamBasicDescription dstFormat = {};
    dstFormat.mSampleRate = srcFormat.mSampleRate;
    dstFormat.mFormatID = kAudioFormatLinearPCM;
    dstFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    dstFormat.mBitsPerChannel = 32;
    dstFormat.mChannelsPerFrame = srcFormat.mChannelsPerFrame;
    dstFormat.mFramesPerPacket = 1;
    dstFormat.mBytesPerFrame = dstFormat.mChannelsPerFrame * sizeof(float);
    dstFormat.mBytesPerPacket = dstFormat.mBytesPerFrame;

    status = ExtAudioFileSetProperty(extAudioFile,
                                     kExtAudioFileProperty_ClientDataFormat,
                                     sizeof(dstFormat),
                                     &dstFormat);
    if (status != noErr) {
        logError("SoundBuffer") << "failed to set the output format";
        ExtAudioFileDispose(extAudioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to set output format (status=" +
                                std::to_string((int)status) + ")");
    }

    // Get total frame count
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    status = ExtAudioFileGetProperty(extAudioFile,
                                     kExtAudioFileProperty_FileLengthFrames,
                                     &propSize,
                                     &totalFrames);
    if (status != noErr || totalFrames <= 0) {
        logError("SoundBuffer") << "failed to get the frame count";
        ExtAudioFileDispose(extAudioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to get frame count (status=" +
                                std::to_string((int)status) + ")");
    }

    // Allocate buffer (the sample count is checked before it is formed)
    size_t sampleCount = 0;
    if (!internal::interleavedSampleCount(static_cast<uint64_t>(totalFrames),
                                          static_cast<int>(dstFormat.mChannelsPerFrame),
                                          samples.max_size(), sampleCount)) {
        logError("SoundBuffer") << "AAC stream too large to load ("
                                << dstFormat.mChannelsPerFrame << " ch, "
                                << static_cast<long long>(totalFrames) << " frames)";
        ExtAudioFileDispose(extAudioFile);
        return LoadResult::fail(LoadError::DecodeFailed, "AAC stream too large to load");
    }
    channels = static_cast<int>(dstFormat.mChannelsPerFrame);
    sampleRate = static_cast<int>(dstFormat.mSampleRate);
    numSamples = static_cast<size_t>(totalFrames);
    samples.resize(sampleCount);

    // Read all frames
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = dstFormat.mChannelsPerFrame;
    bufferList.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples.size() * sizeof(float));
    bufferList.mBuffers[0].mData = samples.data();

    UInt32 framesToRead = static_cast<UInt32>(totalFrames);
    status = ExtAudioFileRead(extAudioFile, &framesToRead, &bufferList);

    ExtAudioFileDispose(extAudioFile);

    if (status != noErr) {
        logError("SoundBuffer") << "failed to read AAC data (status=" << (int)status << ")";
        samples.clear();
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to read AAC data (status=" +
                                std::to_string((int)status) + ")");
    }

    // Update actual sample count (framesToRead <= totalFrames: at most sampleCount)
    numSamples = std::min(static_cast<size_t>(framesToRead), static_cast<size_t>(totalFrames));
    samples.resize(numSamples * static_cast<size_t>(channels));

    logVerbose("SoundBuffer") << "loaded AAC " << pathStr << " (" << channels << " ch, "
                              << sampleRate << " Hz, " << numSamples << " samples)";

    return LoadResult::success();
}

// -----------------------------------------------------------------------------
// SoundBuffer::loadAacFromMemory - macOS implementation (memory-based)
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    if (!data || dataSize == 0) {
        logError("SoundBuffer") << "AAC data is empty";
        return LoadResult::fail(LoadError::DecodeFailed, "empty memory range");
    }

    // Setup memory file data
    MemoryAudioFileData fileData;
    fileData.data = static_cast<const uint8_t*>(data);
    fileData.dataSize = dataSize;
    fileData.position = 0;

    // Open audio file from memory
    AudioFileID audioFile = nullptr;
    OSStatus status = AudioFileOpenWithCallbacks(
        &fileData,
        MemoryAudioFile_ReadProc,
        nullptr,  // Write callback (not needed)
        MemoryAudioFile_GetSizeProc,
        nullptr,  // SetSize callback (not needed)
        kAudioFileAAC_ADTSType,  // Try ADTS first
        &audioFile
    );

    // If ADTS fails, try M4A/MP4 format
    if (status != noErr) {
        status = AudioFileOpenWithCallbacks(
            &fileData,
            MemoryAudioFile_ReadProc,
            nullptr,
            MemoryAudioFile_GetSizeProc,
            nullptr,
            kAudioFileM4AType,
            &audioFile
        );
    }

    // If still fails, try generic
    if (status != noErr) {
        status = AudioFileOpenWithCallbacks(
            &fileData,
            MemoryAudioFile_ReadProc,
            nullptr,
            MemoryAudioFile_GetSizeProc,
            nullptr,
            0,  // Let AudioFile determine type
            &audioFile
        );
    }

    if (status != noErr || !audioFile) {
        logError("SoundBuffer") << "failed to open AAC data (status=" << (int)status << ")";
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to open AAC data (status=" +
                                std::to_string((int)status) + ")");
    }

    // Get source format
    AudioStreamBasicDescription srcFormat;
    UInt32 propSize = sizeof(srcFormat);
    status = AudioFileGetProperty(audioFile, kAudioFilePropertyDataFormat, &propSize, &srcFormat);
    if (status != noErr) {
        logError("SoundBuffer") << "failed to get the AAC format";
        AudioFileClose(audioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to get AAC format (status=" +
                                std::to_string((int)status) + ")");
    }

    // Create ExtAudioFile for conversion
    ExtAudioFileRef extAudioFile = nullptr;
    status = ExtAudioFileWrapAudioFileID(audioFile, false, &extAudioFile);
    if (status != noErr || !extAudioFile) {
        logError("SoundBuffer") << "failed to wrap the audio file";
        AudioFileClose(audioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to wrap audio file (status=" +
                                std::to_string((int)status) + ")");
    }

    // Set output format (32-bit float PCM)
    AudioStreamBasicDescription dstFormat = {};
    dstFormat.mSampleRate = srcFormat.mSampleRate;
    dstFormat.mFormatID = kAudioFormatLinearPCM;
    dstFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    dstFormat.mBitsPerChannel = 32;
    dstFormat.mChannelsPerFrame = srcFormat.mChannelsPerFrame;
    dstFormat.mFramesPerPacket = 1;
    dstFormat.mBytesPerFrame = dstFormat.mChannelsPerFrame * sizeof(float);
    dstFormat.mBytesPerPacket = dstFormat.mBytesPerFrame;

    status = ExtAudioFileSetProperty(extAudioFile,
                                     kExtAudioFileProperty_ClientDataFormat,
                                     sizeof(dstFormat),
                                     &dstFormat);
    if (status != noErr) {
        logError("SoundBuffer") << "failed to set the output format";
        ExtAudioFileDispose(extAudioFile);
        AudioFileClose(audioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to set output format (status=" +
                                std::to_string((int)status) + ")");
    }

    // Get total frame count
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    status = ExtAudioFileGetProperty(extAudioFile,
                                     kExtAudioFileProperty_FileLengthFrames,
                                     &propSize,
                                     &totalFrames);
    if (status != noErr || totalFrames <= 0) {
        logError("SoundBuffer") << "failed to get the frame count";
        ExtAudioFileDispose(extAudioFile);
        AudioFileClose(audioFile);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to get frame count (status=" +
                                std::to_string((int)status) + ")");
    }

    // Allocate buffer (the sample count is checked before it is formed)
    size_t sampleCount = 0;
    if (!internal::interleavedSampleCount(static_cast<uint64_t>(totalFrames),
                                          static_cast<int>(dstFormat.mChannelsPerFrame),
                                          samples.max_size(), sampleCount)) {
        logError("SoundBuffer") << "AAC stream too large to load ("
                                << dstFormat.mChannelsPerFrame << " ch, "
                                << static_cast<long long>(totalFrames) << " frames)";
        ExtAudioFileDispose(extAudioFile);
        AudioFileClose(audioFile);
        return LoadResult::fail(LoadError::DecodeFailed, "AAC stream too large to load");
    }
    channels = static_cast<int>(dstFormat.mChannelsPerFrame);
    sampleRate = static_cast<int>(dstFormat.mSampleRate);
    numSamples = static_cast<size_t>(totalFrames);
    samples.resize(sampleCount);

    // Read all frames
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = dstFormat.mChannelsPerFrame;
    bufferList.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples.size() * sizeof(float));
    bufferList.mBuffers[0].mData = samples.data();

    UInt32 framesToRead = static_cast<UInt32>(totalFrames);
    status = ExtAudioFileRead(extAudioFile, &framesToRead, &bufferList);

    ExtAudioFileDispose(extAudioFile);
    AudioFileClose(audioFile);

    if (status != noErr) {
        logError("SoundBuffer") << "failed to read AAC data (status=" << (int)status << ")";
        samples.clear();
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to read AAC data (status=" +
                                std::to_string((int)status) + ")");
    }

    // Update actual sample count (in case fewer frames were read; framesToRead <= totalFrames: at most sampleCount)
    numSamples = std::min(static_cast<size_t>(framesToRead), static_cast<size_t>(totalFrames));
    samples.resize(numSamples * static_cast<size_t>(channels));

    logVerbose("SoundBuffer") << "decoded AAC from memory (" << channels << " ch, "
                              << sampleRate << " Hz, " << numSamples << " samples)";

    return LoadResult::success();
}

} // namespace trussc
