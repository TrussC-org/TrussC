#pragma once

#include <sstream>
#include <string>
#include "tc/sound/tcSound.h"

// Shared by the per-PR and daily pending-seek checks. Called on failure,
// before stop() releases the stream whose state we need to inspect.
inline std::string streamSeekFailureState(const tc::Sound& sound, float level) {
    const auto s = tc::internal::streamSeekStateForTests(sound);
    std::ostringstream out;
    out << std::boolalpha
        << "level=" << level
        << ", isPlaying()=" << sound.isPlaying()
        << ", getPosition()=" << sound.getPosition()
        << ", hasStream=" << s.hasStream
        << ", seekRequestSeq=" << s.request
        << ", seekServedSeq=" << s.served
        << ", seekPublishedSeq=" << s.published
        << ", seekAppliedSeq=" << s.applied
        << ", endOfStream=" << s.endOfStream
        << ", decoderAtEnd=" << s.decoderAtEnd
        << ", workerPasses=" << s.workerPasses;
    return out.str();
}
