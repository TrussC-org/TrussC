#pragma once

// =============================================================================
// TrussC ChipSound
// Programmatic sound generation for beeps, effects, and simple melodies
//
// Usage:
//   // Simple beep
//   ChipSoundNote note { .wave = Wave::Square, .hz = 880, .duration = 0.1f };
//   Sound beep = note.build();
//   beep.play();
//
//   // Melody
//   ChipSoundBundle melody;
//   melody.add({ .wave = Wave::Square, .hz = 440, .duration = 0.2f }, 0.0f);
//   melody.add({ .wave = Wave::Square, .hz = 554, .duration = 0.2f }, 0.25f);
//   Sound song = melody.build();
//   song.setLoop(true);
//   song.play();
// =============================================================================

#include "tcSound.h"
#include <vector>
#include <algorithm>

namespace trussc {

// ---------------------------------------------------------------------------
// ChipSoundNote - Single tone generator
// ---------------------------------------------------------------------------
struct ChipSoundNote {
    // Waveform type
    enum class Wave { Sin, Square, Triangle, Sawtooth, Noise, PinkNoise, Silent };

    Wave wave = Wave::Sin;
    float hz = 440.0f;          // Frequency (ignored for Noise/Silent)
    float volume = 0.5f;        // 0.0 - 1.0
    float duration = 0.2f;      // seconds

    // ADSR Envelope (all in seconds, sustainLevel is 0.0-1.0)
    float attack = 0.01f;
    float decay = 0.05f;
    float sustain = 0.7f;       // sustain level
    float release = 0.05f;

    // Constructors
    ChipSoundNote() = default;

    ChipSoundNote(Wave w, float freq, float dur, float vol = 0.5f)
        : wave(w), hz(freq), duration(dur), volume(vol) {}

    // Build Sound from parameters
    Sound build() const {
        SoundBuffer buf;
        generateBuffer(buf);
        buf.applyADSR(attack, decay, sustain, release);

        Sound sound;
        sound.loadFromBuffer(buf);
        return sound;
    }

    // Generate raw buffer (without ADSR, for Bundle mixing)
    void generateBuffer(SoundBuffer& buf) const {
        switch (wave) {
            case Wave::Sin:
                buf.generateSineWave(hz, duration, volume);
                break;
            case Wave::Square:
                buf.generateSquareWave(hz, duration, volume);
                break;
            case Wave::Triangle:
                buf.generateTriangleWave(hz, duration, volume);
                break;
            case Wave::Sawtooth:
                buf.generateSawtoothWave(hz, duration, volume);
                break;
            case Wave::Noise:
                buf.generateNoise(duration, volume);
                break;
            case Wave::PinkNoise:
                buf.generatePinkNoise(duration, volume);
                break;
            case Wave::Silent:
                buf.generateSilence(duration);
                break;
        }
    }

    // Total duration including release
    float getTotalDuration() const {
        return duration;
    }
};

// Convenience alias
using Wave = ChipSoundNote::Wave;

// ---------------------------------------------------------------------------
// ChipSoundBundle - Multiple notes with timing
// ---------------------------------------------------------------------------
struct ChipSoundBundle {
    struct Entry {
        ChipSoundNote note;
        float time;  // Start time in seconds
    };

    std::vector<Entry> entries;
    float volume = 1.0f;  // Master volume multiplier

    // Add a note at specified time
    void add(const ChipSoundNote& note, float time) {
        entries.push_back({note, time});
    }

    // Convenience: add with inline parameters
    void add(ChipSoundNote::Wave wave, float hz, float duration, float time, float vol = 0.5f) {
        entries.push_back({ChipSoundNote(wave, hz, duration, vol), time});
    }

    // Clear all entries
    void clear() {
        entries.clear();
    }

    // Get total duration (auto-calculated from last note end)
    float getDuration() const {
        float maxEnd = 0.0f;
        for (const auto& e : entries) {
            float end = e.time + e.note.getTotalDuration();
            if (end > maxEnd) maxEnd = end;
        }
        return maxEnd;
    }

    // Build mixed Sound from all entries
    Sound build() const {
        if (entries.empty()) {
            Sound empty;
            return empty;
        }

        float totalDuration = getDuration();
        constexpr int sampleRate = 44100;

        // Create base buffer with silence
        SoundBuffer mixed;
        mixed.generateSilence(totalDuration, sampleRate);

        // Mix each note
        for (const auto& e : entries) {
            SoundBuffer noteBuf;
            e.note.generateBuffer(noteBuf);
            noteBuf.applyADSR(e.note.attack, e.note.decay, e.note.sustain, e.note.release);

            size_t offsetSamples = (size_t)(e.time * sampleRate);
            mixed.mixFrom(noteBuf, offsetSamples, volume);
        }

        // Clip to prevent distortion
        mixed.clip();

        Sound sound;
        sound.loadFromBuffer(mixed);
        return sound;
    }
};

} // namespace trussc
