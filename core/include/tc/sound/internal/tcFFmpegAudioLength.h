#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

namespace trussc::internal {
// Audio-stream duration excludes encoder tail padding. Container-wide duration
// is only a hint and must not truncate decoded audio. Return unknown unchanged.
inline int64_t ffmpegAudioStreamLength(const AVStream* stream, int sampleRate) {
    if (stream->duration == AV_NOPTS_VALUE || stream->duration <= 0) return AV_NOPTS_VALUE;
    return av_rescale_q(stream->duration, stream->time_base, AVRational{1, sampleRate});
}
} // namespace trussc::internal
