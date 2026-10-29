#pragma once
#include "tc/sound/internal/tcAudioDecode.h"

namespace trussc::internal {
// AudioToolbox owns its codec allocations; PCM grows as frames are read.
inline LoadResult decodeAudioToolbox(AudioFileID file, SoundBuffer& out, AudioDecodeStatus& status,
                                     const std::string& input) {
    AudioStreamBasicDescription source{};
    UInt32 bytes = sizeof(source);
    if (AudioFileGetProperty(file, kAudioFilePropertyDataFormat, &bytes, &source) != noErr ||
        (source.mChannelsPerFrame < 1 || !std::isfinite(source.mSampleRate) || source.mSampleRate < 1 ||
         source.mChannelsPerFrame > UINT32_MAX / sizeof(float)))
        return LoadResult::fail(LoadError::DecodeFailed, "invalid AAC format");
    if (!status.sampleRateFits(source.mSampleRate, input))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC sample rate does not fit SoundBuffer's int sample rate");
    ExtAudioFileRef ext = nullptr;
    if (ExtAudioFileWrapAudioFileID(file, false, &ext) != noErr || !ext)
        return LoadResult::fail(LoadError::DecodeFailed, "failed to open AAC converter");
    struct Close { ExtAudioFileRef file; ~Close() { ExtAudioFileDispose(file); } } close{ext};
    AudioStreamBasicDescription dest{};
    dest.mSampleRate = source.mSampleRate;
    dest.mFormatID = kAudioFormatLinearPCM;
    dest.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    dest.mBitsPerChannel = 32;
    dest.mChannelsPerFrame = source.mChannelsPerFrame;
    dest.mFramesPerPacket = 1;
    dest.mBytesPerFrame = dest.mBytesPerPacket = source.mChannelsPerFrame * sizeof(float);
    if (ExtAudioFileSetProperty(ext, kExtAudioFileProperty_ClientDataFormat, sizeof(dest), &dest) != noErr)
        return LoadResult::fail(LoadError::DecodeFailed, "failed to configure AAC converter");
    // Each SDK read uses a UInt32 byte count. Reduce the chunk if needed.
    const UInt32 chunkFrames = std::min<UInt32>(4096, UINT32_MAX / dest.mBytesPerFrame);
    std::vector<float> pcm, chunk;
    if (!growAudioVector(chunk, size_t(chunkFrames) * source.mChannelsPerFrame, status))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed");
    chunk.resize(size_t(chunkFrames) * source.mChannelsPerFrame);
    for (;;) {
        UInt32 frames = chunkFrames;
        AudioBufferList buffers{};
        buffers.mNumberBuffers = 1;
        buffers.mBuffers[0].mNumberChannels = source.mChannelsPerFrame;
        buffers.mBuffers[0].mDataByteSize = UInt32(chunk.size() * sizeof(float));
        buffers.mBuffers[0].mData = chunk.data();
        if (ExtAudioFileRead(ext, &frames, &buffers) != noErr || frames > chunkFrames)
            return LoadResult::fail(LoadError::DecodeFailed, "AAC read failed");
        if (!frames) break;
        // Re-query source format so a changed stream cannot be published with
        // the original layout. Client output storage remains a fixed chunk.
        AudioStreamBasicDescription current{};
        bytes = sizeof(current);
        if (ExtAudioFileGetProperty(ext, kExtAudioFileProperty_FileDataFormat, &bytes, &current) != noErr ||
            current.mChannelsPerFrame != source.mChannelsPerFrame || current.mSampleRate != source.mSampleRate) {
            status.reject("AAC format change", current.mChannelsPerFrame, source.mChannelsPerFrame);
            return LoadResult::fail(LoadError::DecodeFailed, "AAC format changed");
        }
        if (!appendAudio(pcm, chunk.data(), size_t(frames) * source.mChannelsPerFrame, status))
            return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed");
    }
    if (pcm.empty()) return LoadResult::fail(LoadError::DecodeFailed, "no AAC samples");
    out.channels = int(source.mChannelsPerFrame);
    out.sampleRate = int(source.mSampleRate);
    out.numSamples = pcm.size() / source.mChannelsPerFrame;
    out.samples = std::move(pcm);
    return LoadResult::success();
}
} // namespace trussc::internal
