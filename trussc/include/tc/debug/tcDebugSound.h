#pragma once

// =============================================================================
// TrussC Debug Sound - Simple beep functions for debugging
//
// Usage:
//   dbg::beep();                   // Default ping sound
//   dbg::beep(dbg::Beep::success); // Success sound (pico)
//   dbg::beep(dbg::Beep::error);   // Error sound (boo)
//   dbg::beep(dbg::Beep::click);   // UI click
//   dbg::beep(dbg::Beep::coin);    // Game coin sound
//   dbg::beep(880.0f);             // Custom frequency
//   dbg::setBeepVolume(0.3f);      // Set volume (0.0-1.0)
//
// Available presets (organized by category):
//
//   [Basic]
//   ping     - Single beep (default)
//
//   [Positive]
//   success  - Two-tone rising (pico)
//   complete - Task completion fanfare
//   coin     - Game item pickup (sparkly)
//
//   [Negative]
//   error    - Low buzz (boo)
//   warning  - Attention (two short beeps)
//   cancel   - Cancel/back
//
//   [UI Feedback]
//   click    - UI selection click
//   typing   - Key input feedback
//   notify   - Two-tone notification
//
//   [Transition]
//   sweep    - Screen transition whoosh
//
// Features:
//   - Sounds are cached after first generation
//   - Same-frame calls are debounced (only plays once per frame)
//   - Max 128 cached sounds (prevents memory bloat)
// =============================================================================

#include <unordered_map>
#include <memory>
#include "../sound/tcSound.h"

namespace trussc {

// Forward declaration for frame count access
namespace internal {
    extern uint64_t updateFrameCount;
}

namespace dbg {

// Preset sound types (organized by category)
enum class Beep {
    // Basic
    ping,       // Single beep (default)

    // Positive feedback
    success,    // Two-tone rising (pico)
    complete,   // Task completion fanfare
    coin,       // Game item pickup (sparkly)

    // Negative feedback
    error,      // Low buzz (boo)
    warning,    // Attention (two short beeps)
    cancel,     // Cancel/back

    // UI feedback
    click,      // UI selection click
    typing,     // Key input feedback
    notify,     // Two-tone notification

    // Transition
    sweep       // Screen transition whoosh
};

namespace detail {

// Cache key for preset sounds (negative to avoid collision with frequencies)
inline int presetToKey(Beep type) {
    return -static_cast<int>(type) - 1;
}

// Internal state manager
struct BeepManager {
    std::unordered_map<int, std::shared_ptr<Sound>> cache;
    uint64_t lastBeepFrame = 0;
    float volume = 0.5f;
    static constexpr size_t MAX_CACHE_SIZE = 128;

    // Generate sound for a preset type
    std::shared_ptr<Sound> generatePreset(Beep type) {
        auto sound = std::make_shared<Sound>();
        SoundBuffer buffer;

        switch (type) {

            // =========== Basic ===========

            case Beep::ping: {
                // Single short beep at 880Hz
                buffer.generateSineWave(880.0f, 0.08f, volume);
                buffer.applyADSR(0.005f, 0.02f, 0.3f, 0.05f);
                break;
            }

            // =========== Positive ===========

            case Beep::success: {
                // Two-tone rising (pico): 880Hz -> 1100Hz
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.08f, volume);
                b1.applyADSR(0.005f, 0.02f, 0.5f, 0.03f);
                b2.generateSineWave(1100.0f, 0.1f, volume);
                b2.applyADSR(0.005f, 0.02f, 0.5f, 0.05f);

                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.07f * 44100), 1.0f);
                buffer.clip();
                break;
            }

            case Beep::complete: {
                // Fanfare-like rising completion sound
                SoundBuffer b1, b2, b3, b4;
                b1.generateSineWave(523.0f, 0.1f, volume * 0.7f);   // C5
                b1.applyADSR(0.005f, 0.03f, 0.5f, 0.04f);
                b2.generateSineWave(659.0f, 0.1f, volume * 0.8f);   // E5
                b2.applyADSR(0.005f, 0.03f, 0.5f, 0.04f);
                b3.generateSineWave(784.0f, 0.1f, volume * 0.9f);   // G5
                b3.applyADSR(0.005f, 0.03f, 0.5f, 0.04f);
                b4.generateSineWave(1047.0f, 0.2f, volume);         // C6
                b4.applyADSR(0.005f, 0.05f, 0.6f, 0.1f);

                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.08f * 44100), 1.0f);
                buffer.mixFrom(b3, static_cast<size_t>(0.16f * 44100), 1.0f);
                buffer.mixFrom(b4, static_cast<size_t>(0.24f * 44100), 1.0f);
                buffer.clip();
                break;
            }

            case Beep::coin: {
                // Game coin sound - 2-note arpeggio with sparkly timbre
                // First note: E6 with shimmer
                SoundBuffer n1_main, n1_oct, n1_det;
                n1_main.generateSineWave(1318.5f, 0.1f, volume * 0.5f);   // E6
                n1_main.applyADSR(0.001f, 0.02f, 0.3f, 0.04f);
                n1_oct.generateSineWave(2637.0f, 0.08f, volume * 0.3f);   // E7 (octave)
                n1_oct.applyADSR(0.001f, 0.015f, 0.2f, 0.03f);
                n1_det.generateSineWave(1324.0f, 0.1f, volume * 0.4f);    // Detuned for shimmer
                n1_det.applyADSR(0.001f, 0.02f, 0.3f, 0.04f);

                // Second note: B6 with shimmer (higher)
                SoundBuffer n2_main, n2_oct, n2_det;
                n2_main.generateSineWave(1975.5f, 0.12f, volume * 0.5f);  // B6
                n2_main.applyADSR(0.001f, 0.025f, 0.35f, 0.05f);
                n2_oct.generateSineWave(3951.0f, 0.1f, volume * 0.25f);   // B7 (octave)
                n2_oct.applyADSR(0.001f, 0.02f, 0.2f, 0.04f);
                n2_det.generateSineWave(1982.0f, 0.12f, volume * 0.4f);   // Detuned
                n2_det.applyADSR(0.001f, 0.025f, 0.35f, 0.05f);

                // Mix first note
                buffer = n1_main;
                buffer.mixFrom(n1_oct, 0, 1.0f);
                buffer.mixFrom(n1_det, 0, 1.0f);

                // Mix second note with offset
                size_t offset = static_cast<size_t>(0.06f * 44100);
                buffer.mixFrom(n2_main, offset, 1.0f);
                buffer.mixFrom(n2_oct, offset, 1.0f);
                buffer.mixFrom(n2_det, offset, 1.0f);
                buffer.clip();
                break;
            }

            // =========== Negative ===========

            case Beep::error: {
                // Low buzz at 220Hz with square wave character
                // Lower volume (0.4x) because square wave is perceptually louder
                buffer.generateSquareWave(220.0f, 0.25f, volume * 0.4f);
                buffer.applyADSR(0.01f, 0.05f, 0.6f, 0.1f);
                break;
            }

            case Beep::warning: {
                // Two short beeps - attention grabbing
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.06f, volume * 0.8f);
                b1.applyADSR(0.002f, 0.02f, 0.5f, 0.02f);
                b2.generateSineWave(880.0f, 0.06f, volume * 0.8f);
                b2.applyADSR(0.002f, 0.02f, 0.5f, 0.02f);

                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.1f * 44100), 1.0f);
                buffer.clip();
                break;
            }

            case Beep::cancel: {
                // Short descending tone
                SoundBuffer b1, b2;
                b1.generateSineWave(440.0f, 0.05f, volume * 0.6f);
                b1.applyADSR(0.002f, 0.02f, 0.4f, 0.02f);
                b2.generateSineWave(330.0f, 0.08f, volume * 0.5f);
                b2.applyADSR(0.002f, 0.02f, 0.3f, 0.04f);

                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.04f * 44100), 1.0f);
                buffer.clip();
                break;
            }

            // =========== UI Feedback ===========

            case Beep::click: {
                // Short click sound - very brief high frequency
                buffer.generateSineWave(1200.0f, 0.02f, volume * 0.6f);
                buffer.applyADSR(0.001f, 0.01f, 0.2f, 0.01f);
                break;
            }

            case Beep::typing: {
                // Very short click for typing feedback
                buffer.generateSineWave(600.0f, 0.015f, volume * 0.3f);
                buffer.applyADSR(0.001f, 0.005f, 0.2f, 0.005f);
                // Add slight noise for mechanical feel
                SoundBuffer noise;
                noise.generateNoise(0.01f, volume * 0.1f);
                noise.applyADSR(0.001f, 0.003f, 0.1f, 0.003f);
                buffer.mixFrom(noise, 0, 1.0f);
                buffer.clip();
                break;
            }

            case Beep::notify: {
                // Two-tone falling (ping-pong): 880Hz -> 660Hz
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.1f, volume);
                b1.applyADSR(0.005f, 0.03f, 0.5f, 0.05f);
                b2.generateSineWave(660.0f, 0.12f, volume);
                b2.applyADSR(0.005f, 0.03f, 0.5f, 0.07f);

                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.12f * 44100), 1.0f);
                buffer.clip();
                break;
            }

            // =========== Transition ===========

            case Beep::sweep: {
                // Whoosh/swoosh sound for transitions (シュッ)
                float duration = 0.12f;
                int sr = 44100;
                int numSamples = static_cast<int>(duration * sr);

                buffer.samples.resize(numSamples);
                buffer.channels = 1;
                buffer.sampleRate = sr;
                buffer.numSamples = numSamples;

                for (int i = 0; i < numSamples; i++) {
                    float t = static_cast<float>(i) / sr;
                    float progress = static_cast<float>(i) / numSamples;

                    // Exponential frequency sweep (more dramatic)
                    float freq = 300.0f * std::pow(6.0f, progress);  // 300Hz -> 1800Hz exponential

                    // Bell-shaped envelope (quick attack, sustain, fade)
                    float env = std::sin(progress * 3.14159f);
                    env = env * env;  // Sharper curve

                    // Add slight noise/breath for whoosh character
                    float noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.15f;

                    float sample = (std::sin(2.0f * 3.14159f * freq * t) + noise * env)
                                   * env * volume * 0.4f;
                    buffer.samples[i] = sample;
                }
                break;
            }
        }

        sound->loadFromBuffer(buffer);
        return sound;
    }

    // Generate sound for a custom frequency
    std::shared_ptr<Sound> generateFrequency(float freq) {
        auto sound = std::make_shared<Sound>();
        SoundBuffer buffer;
        buffer.generateSineWave(freq, 0.1f, volume);
        buffer.applyADSR(0.005f, 0.02f, 0.4f, 0.05f);
        sound->loadFromBuffer(buffer);
        return sound;
    }

    // Play a preset sound
    void playPreset(Beep type) {
        // Debounce: skip if already played this frame
        uint64_t currentFrame = internal::updateFrameCount;
        if (currentFrame == lastBeepFrame && currentFrame > 0) {
            return;
        }
        lastBeepFrame = currentFrame;

        int key = presetToKey(type);
        auto it = cache.find(key);
        if (it == cache.end()) {
            // Generate and cache
            if (cache.size() >= MAX_CACHE_SIZE) {
                // Simple eviction: clear oldest half
                // (In practice, 128 sounds is plenty and this rarely triggers)
                cache.clear();
            }
            auto sound = generatePreset(type);
            cache[key] = sound;
            sound->play();
        } else {
            it->second->play();
        }
    }

    // Play a custom frequency
    void playFrequency(float freq) {
        // Debounce: skip if already played this frame
        uint64_t currentFrame = internal::updateFrameCount;
        if (currentFrame == lastBeepFrame && currentFrame > 0) {
            return;
        }
        lastBeepFrame = currentFrame;

        // Round frequency to int for cache key
        int key = static_cast<int>(freq);
        auto it = cache.find(key);
        if (it == cache.end()) {
            if (cache.size() >= MAX_CACHE_SIZE) {
                cache.clear();
            }
            auto sound = generateFrequency(freq);
            cache[key] = sound;
            sound->play();
        } else {
            it->second->play();
        }
    }

    void setVolume(float vol) {
        volume = (vol < 0.0f) ? 0.0f : (vol > 1.0f) ? 1.0f : vol;
        // Clear cache so new sounds use updated volume
        cache.clear();
    }
};

inline BeepManager& getManager() {
    static BeepManager manager;
    return manager;
}

} // namespace detail

// =============================================================================
// Public API
// =============================================================================

// Play default beep (ping)
inline void beep() {
    detail::getManager().playPreset(Beep::ping);
}

// Play preset sound
inline void beep(Beep type) {
    detail::getManager().playPreset(type);
}

// Play custom frequency
inline void beep(float frequency) {
    detail::getManager().playFrequency(frequency);
}

// Play custom frequency (int overload)
inline void beep(int frequency) {
    detail::getManager().playFrequency(static_cast<float>(frequency));
}

// Set beep volume (0.0-1.0)
inline void setBeepVolume(float vol) {
    detail::getManager().setVolume(vol);
}

// Get current beep volume
inline float getBeepVolume() {
    return detail::getManager().volume;
}

} // namespace dbg
} // namespace trussc

