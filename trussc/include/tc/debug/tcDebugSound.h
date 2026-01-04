#pragma once

// =============================================================================
// TrussC Debug Sound - Simple beep functions for debugging
//
// Usage:
//   dbg::beep();                   // Default ping sound
//   dbg::beep(dbg::Beep::success); // Success sound (pico)
//   dbg::beep(dbg::Beep::error);   // Error sound (boo)
//   dbg::beep(880.0f);             // Custom frequency
//   dbg::setBeepVolume(0.3f);      // Set volume (0.0-1.0)
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

// Preset sound types
enum class Beep {
    ping,     // Single beep (default)
    success,  // Two-tone rising (pico)
    error,    // Low buzz (boo)
    notify    // Two-tone falling (ping-pong)
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
            case Beep::ping: {
                // Single short beep at 880Hz
                buffer.generateSineWave(880.0f, 0.08f, volume);
                buffer.applyADSR(0.005f, 0.02f, 0.3f, 0.05f);
                break;
            }
            case Beep::success: {
                // Two-tone rising (pico): 880Hz -> 1100Hz
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.08f, volume);
                b1.applyADSR(0.005f, 0.02f, 0.5f, 0.03f);
                b2.generateSineWave(1100.0f, 0.1f, volume);
                b2.applyADSR(0.005f, 0.02f, 0.5f, 0.05f);

                buffer = b1;
                size_t offset = static_cast<size_t>(0.07f * 44100);
                buffer.mixFrom(b2, offset, 1.0f);
                buffer.clip();
                break;
            }
            case Beep::error: {
                // Low buzz at 220Hz with square wave character
                // Lower volume (0.4x) because square wave is perceptually louder
                buffer.generateSquareWave(220.0f, 0.25f, volume * 0.4f);
                buffer.applyADSR(0.01f, 0.05f, 0.6f, 0.1f);
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
                size_t offset = static_cast<size_t>(0.12f * 44100);
                buffer.mixFrom(b2, offset, 1.0f);
                buffer.clip();
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

