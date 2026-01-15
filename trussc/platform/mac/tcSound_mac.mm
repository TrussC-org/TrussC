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
bool SoundBuffer::loadAac(const std::string& path) {
    // Convert path to URL
    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSURL* fileURL = [NSURL fileURLWithPath:nsPath];

    if (!fileURL) {
        printf("SoundBuffer: invalid path: %s\n", path.c_str());
        return false;
    }

    // Open audio file
    ExtAudioFileRef extAudioFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL((__bridge CFURLRef)fileURL, &extAudioFile);

    if (status != noErr || !extAudioFile) {
        printf("SoundBuffer: Failed to open AAC file: %s (status: %d)\n", path.c_str(), (int)status);
        return false;
    }

    // Get source format
    AudioStreamBasicDescription srcFormat;
    UInt32 propSize = sizeof(srcFormat);
    status = ExtAudioFileGetProperty(extAudioFile,
                                     kExtAudioFileProperty_FileDataFormat,
                                     &propSize,
                                     &srcFormat);
    if (status != noErr) {
        printf("SoundBuffer: Failed to get AAC format\n");
        ExtAudioFileDispose(extAudioFile);
        return false;
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
        printf("SoundBuffer: Failed to set output format\n");
        ExtAudioFileDispose(extAudioFile);
        return false;
    }

    // Get total frame count
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    status = ExtAudioFileGetProperty(extAudioFile,
                                     kExtAudioFileProperty_FileLengthFrames,
                                     &propSize,
                                     &totalFrames);
    if (status != noErr || totalFrames <= 0) {
        printf("SoundBuffer: Failed to get frame count\n");
        ExtAudioFileDispose(extAudioFile);
        return false;
    }

    // Allocate buffer
    channels = static_cast<int>(dstFormat.mChannelsPerFrame);
    sampleRate = static_cast<int>(dstFormat.mSampleRate);
    numSamples = static_cast<size_t>(totalFrames);
    samples.resize(numSamples * channels);

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
        printf("SoundBuffer: Failed to read AAC data (status: %d)\n", (int)status);
        samples.clear();
        return false;
    }

    // Update actual sample count
    numSamples = framesToRead;
    samples.resize(numSamples * channels);

    printf("SoundBuffer: loaded AAC %s (%d ch, %d Hz, %zu samples)\n",
           path.c_str(), channels, sampleRate, numSamples);

    return true;
}

// -----------------------------------------------------------------------------
// SoundBuffer::loadAacFromMemory - macOS implementation (memory-based)
// -----------------------------------------------------------------------------
bool SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    if (!data || dataSize == 0) {
        printf("SoundBuffer: AAC data is empty\n");
        return false;
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
        printf("SoundBuffer: Failed to open AAC data (status: %d)\n", (int)status);
        return false;
    }

    // Get source format
    AudioStreamBasicDescription srcFormat;
    UInt32 propSize = sizeof(srcFormat);
    status = AudioFileGetProperty(audioFile, kAudioFilePropertyDataFormat, &propSize, &srcFormat);
    if (status != noErr) {
        printf("SoundBuffer: Failed to get AAC format\n");
        AudioFileClose(audioFile);
        return false;
    }

    // Create ExtAudioFile for conversion
    ExtAudioFileRef extAudioFile = nullptr;
    status = ExtAudioFileWrapAudioFileID(audioFile, false, &extAudioFile);
    if (status != noErr || !extAudioFile) {
        printf("SoundBuffer: Failed to wrap audio file\n");
        AudioFileClose(audioFile);
        return false;
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
        printf("SoundBuffer: Failed to set output format\n");
        ExtAudioFileDispose(extAudioFile);
        AudioFileClose(audioFile);
        return false;
    }

    // Get total frame count
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    status = ExtAudioFileGetProperty(extAudioFile,
                                     kExtAudioFileProperty_FileLengthFrames,
                                     &propSize,
                                     &totalFrames);
    if (status != noErr || totalFrames <= 0) {
        printf("SoundBuffer: Failed to get frame count\n");
        ExtAudioFileDispose(extAudioFile);
        AudioFileClose(audioFile);
        return false;
    }

    // Allocate buffer
    channels = static_cast<int>(dstFormat.mChannelsPerFrame);
    sampleRate = static_cast<int>(dstFormat.mSampleRate);
    numSamples = static_cast<size_t>(totalFrames);
    samples.resize(numSamples * channels);

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
        printf("SoundBuffer: Failed to read AAC data (status: %d)\n", (int)status);
        samples.clear();
        return false;
    }

    // Update actual sample count (in case fewer frames were read)
    numSamples = framesToRead;
    samples.resize(numSamples * channels);

    printf("SoundBuffer: decoded AAC from memory (%d ch, %d Hz, %zu samples)\n",
           channels, sampleRate, numSamples);

    return true;
}

} // namespace trussc
