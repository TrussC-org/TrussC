#pragma once
#include "tc/sound/internal/tcAudioDecodeBudget.h"

namespace trussc::internal {
// AudioFile metadata is inspected before requesting PCM conversion. The OS
// decoder's private allocations are not controlled by our working allocator.
inline LoadResult decodeAudioToolbox(AudioFileID file, SoundBuffer& out, AudioDecodeBudget& budget) {
    AudioStreamBasicDescription source{};
    UInt32 bytes = sizeof(source);
    if (AudioFileGetProperty(file, kAudioFilePropertyDataFormat, &bytes, &source) != noErr ||
        !budget.format(source.mChannelsPerFrame, source.mSampleRate))
        return LoadResult::fail(LoadError::DecodeFailed, "invalid AAC format");
    UInt64 packets = 0;
    bytes = sizeof(packets);
    if (source.mFramesPerPacket &&
        AudioFileGetProperty(file, kAudioFilePropertyAudioDataPacketCount, &bytes, &packets) == noErr) {
        if (packets > UINT64_MAX / source.mFramesPerPacket) {
            budget.reject("AAC packet frames", packets, UINT64_MAX / source.mFramesPerPacket);
            return LoadResult::fail(LoadError::DecodeFailed, "AAC length rejected; use SoundStream::loadStream()");
        }
        if (!budget.pcm(packets * source.mFramesPerPacket, source.mChannelsPerFrame))
            return LoadResult::fail(LoadError::DecodeFailed, "AAC length rejected; use SoundStream::loadStream()");
    }
    ExtAudioFileRef ext = nullptr;
    if (ExtAudioFileWrapAudioFileID(file, false, &ext) != noErr || !ext)
        return LoadResult::fail(LoadError::DecodeFailed, "failed to open AAC converter");
    struct Close { ExtAudioFileRef file; ~Close() { ExtAudioFileDispose(file); } } close{ext};
    SInt64 stated = 0;
    bytes = sizeof(stated);
    if (ExtAudioFileGetProperty(ext, kExtAudioFileProperty_FileLengthFrames, &bytes, &stated) != noErr || stated < 0 ||
        !budget.pcm(uint64_t(stated), source.mChannelsPerFrame))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC length rejected; use SoundStream::loadStream()");
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
    // Bound (security): scratch holds one 4096-frame read. Overflow continues in later reads; encoded-input/format rejection rejects the whole sound and is counted.
    std::vector<float> pcm, chunk;
    if (!growAudioVector(chunk, 4096 * source.mChannelsPerFrame, budget))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed");
    chunk.resize(4096 * source.mChannelsPerFrame);
    for (;;) {
        UInt32 frames = 4096;
        AudioBufferList buffers{};
        buffers.mNumberBuffers = 1;
        buffers.mBuffers[0].mNumberChannels = source.mChannelsPerFrame;
        buffers.mBuffers[0].mDataByteSize = UInt32(chunk.size() * sizeof(float));
        buffers.mBuffers[0].mData = chunk.data();
        if (ExtAudioFileRead(ext, &frames, &buffers) != noErr || frames > 4096)
            return LoadResult::fail(LoadError::DecodeFailed, "AAC read failed");
        if (!frames) break;
        // Re-query source format so a changed stream cannot be published with
        // the original layout. Client output storage remains a fixed chunk.
        AudioStreamBasicDescription current{};
        bytes = sizeof(current);
        if (ExtAudioFileGetProperty(ext, kExtAudioFileProperty_FileDataFormat, &bytes, &current) != noErr ||
            !budget.format(current.mChannelsPerFrame, current.mSampleRate) ||
            current.mChannelsPerFrame != source.mChannelsPerFrame || current.mSampleRate != source.mSampleRate) {
            budget.reject("AAC format change", current.mChannelsPerFrame, source.mChannelsPerFrame);
            return LoadResult::fail(LoadError::DecodeFailed, "AAC format changed");
        }
        if (!appendAudio(pcm, chunk.data(), size_t(frames) * source.mChannelsPerFrame, budget,
                         chunk.capacity() * sizeof(float)))
            return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed; use SoundStream::loadStream()");
    }
    if (pcm.empty()) return LoadResult::fail(LoadError::DecodeFailed, "no AAC samples");
    out.channels = int(source.mChannelsPerFrame);
    out.sampleRate = int(source.mSampleRate);
    out.numSamples = pcm.size() / source.mChannelsPerFrame;
    out.samples = std::move(pcm);
    return LoadResult::success();
}
} // namespace trussc::internal
