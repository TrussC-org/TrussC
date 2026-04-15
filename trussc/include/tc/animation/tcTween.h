#pragma once

#include "tcEasing.h"
#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"
#include "../events/tcCoreEvents.h"
#include <memory>
#include <functional>

namespace trussc {

// Forward declaration
inline double getDeltaTime();

// Tween class for animating values with easing
// Works with any type that supports lerp (float, Vec2, Vec3, Vec4, Color, etc.)
// Automatically updates via events().update — no manual update() call needed.
template<typename T>
class Tween {
public:
    // Completion event - fired when animation finishes (all loops done)
    // Stored as unique_ptr to allow move semantics (Event itself stays at stable address)
    std::unique_ptr<Event<void>> complete;

    // Default constructor
    Tween() : complete(std::make_unique<Event<void>>()) {}

    // Move constructor — re-register listener with new this
    Tween(Tween&& other) noexcept
        : complete(std::move(other.complete))
        , start_(other.start_)
        , end_(other.end_)
        , duration_(other.duration_)
        , elapsed_(other.elapsed_)
        , easeType_(other.easeType_)
        , easeTypeOut_(other.easeTypeOut_)
        , mode_(other.mode_)
        , playing_(other.playing_)
        , completed_(other.completed_)
        , asymmetric_(other.asymmetric_)
        , delay_(other.delay_)
        , delayRemaining_(other.delayRemaining_)
        , loopTotal_(other.loopTotal_)
        , loopRemaining_(other.loopRemaining_)
        , loopCount_(other.loopCount_)
        , yoyo_(other.yoyo_)
        , yoyoReversed_(other.yoyoReversed_)
    {
        // Disconnect old listener (it captures old &other)
        other.updateListener_.disconnect();
        other.playing_ = false;

        // Re-register with new this if playing
        if (playing_) {
            registerListener();
        }
    }

    // Move assignment — re-register listener with new this
    Tween& operator=(Tween&& other) noexcept {
        if (this != &other) {
            // Disconnect our current listener
            updateListener_.disconnect();

            complete = std::move(other.complete);
            start_ = other.start_;
            end_ = other.end_;
            duration_ = other.duration_;
            elapsed_ = other.elapsed_;
            easeType_ = other.easeType_;
            easeTypeOut_ = other.easeTypeOut_;
            mode_ = other.mode_;
            playing_ = other.playing_;
            completed_ = other.completed_;
            asymmetric_ = other.asymmetric_;
            delay_ = other.delay_;
            delayRemaining_ = other.delayRemaining_;
            loopTotal_ = other.loopTotal_;
            loopRemaining_ = other.loopRemaining_;
            loopCount_ = other.loopCount_;
            yoyo_ = other.yoyo_;
            yoyoReversed_ = other.yoyoReversed_;

            // Disconnect old listener
            other.updateListener_.disconnect();
            other.playing_ = false;

            // Re-register with new this
            if (playing_) {
                registerListener();
            }
        }
        return *this;
    }

    // No copy
    Tween(const Tween&) = delete;
    Tween& operator=(const Tween&) = delete;

    // Full constructor
    Tween(T start, T end, float duration,
          EaseType type = EaseType::Cubic,
          EaseMode mode = EaseMode::InOut)
        : complete(std::make_unique<Event<void>>())
        , start_(start)
        , end_(end)
        , duration_(duration)
        , easeType_(type)
        , easeTypeOut_(type)
        , mode_(mode) {}

    // ----- Chainable setters -----

    Tween& from(T value) {
        start_ = value;
        return *this;
    }

    Tween& to(T value) {
        end_ = value;
        return *this;
    }

    Tween& duration(float seconds) {
        duration_ = seconds;
        return *this;
    }

    Tween& ease(EaseType type, EaseMode mode = EaseMode::InOut) {
        easeType_ = type;
        easeTypeOut_ = type;
        mode_ = mode;
        asymmetric_ = false;
        return *this;
    }

    // Asymmetric ease: different types for in and out
    Tween& ease(EaseType inType, EaseType outType) {
        easeType_ = inType;
        easeTypeOut_ = outType;
        mode_ = EaseMode::InOut;
        asymmetric_ = true;
        return *this;
    }

    // Loop control: -1 = infinite, 0 = no loop (default), N = repeat N times
    Tween& loop(int count = -1) {
        loopTotal_ = count;
        return *this;
    }

    // Yoyo: reverse direction each loop iteration
    Tween& yoyo(bool enable = true) {
        yoyo_ = enable;
        return *this;
    }

    // Delay before animation starts (seconds)
    Tween& delay(float seconds) {
        delay_ = seconds;
        return *this;
    }

    // ----- Control (chainable) -----

    Tween& start() {
        elapsed_ = 0.0f;
        delayRemaining_ = delay_;
        playing_ = true;
        completed_ = false;
        loopRemaining_ = loopTotal_;
        loopCount_ = 0;
        yoyoReversed_ = false;

        // Register update listener (re-assignment disconnects previous)
        registerListener();
        return *this;
    }

    Tween& pause() {
        playing_ = false;
        updateListener_.disconnect();
        return *this;
    }

    Tween& resume() {
        if (!completed_ && !playing_) {
            playing_ = true;
            registerListener();
        }
        return *this;
    }

    Tween& reset() {
        elapsed_ = 0.0f;
        playing_ = false;
        completed_ = false;
        loopCount_ = 0;
        yoyoReversed_ = false;
        updateListener_.disconnect();
        return *this;
    }

    // Finish immediately (jump to end)
    Tween& finish() {
        elapsed_ = duration_;
        playing_ = false;
        updateListener_.disconnect();
        if (!completed_) {
            completed_ = true;
            if (complete) complete->notify();
        }
        return *this;
    }

    // ----- Getters -----

    T getValue() const {
        float t = getProgress();
        float easedT = applyEasing(t);
        if (yoyoReversed_) {
            return lerpValue(end_, start_, easedT);
        }
        return lerpValue(start_, end_, easedT);
    }

    float getProgress() const {
        if (duration_ <= 0.0f) return 1.0f;
        return std::min(elapsed_ / duration_, 1.0f);
    }

    float getElapsed() const { return elapsed_; }
    float getDuration() const { return duration_; }

    bool isPlaying() const { return playing_; }
    bool isComplete() const { return completed_; }

    T getStart() const { return start_; }
    T getEnd() const { return end_; }

    int getLoopCount() const { return loopCount_; }

private:
    T start_{};
    T end_{};
    float duration_ = 1.0f;
    float elapsed_ = 0.0f;
    EaseType easeType_ = EaseType::Cubic;
    EaseType easeTypeOut_ = EaseType::Cubic;
    EaseMode mode_ = EaseMode::InOut;
    bool playing_ = false;
    bool completed_ = false;
    bool asymmetric_ = false;

    // Delay
    float delay_ = 0.0f;
    float delayRemaining_ = 0.0f;

    // Loop state
    int loopTotal_ = 0;       // -1=infinite, 0=no loop, N=repeat N times
    int loopRemaining_ = 0;
    int loopCount_ = 0;       // How many loops completed so far
    bool yoyo_ = false;
    bool yoyoReversed_ = false;

    // Auto-update listener
    EventListener updateListener_;

    void registerListener() {
        updateListener_ = events().update.listen([this]() {
            this->update(static_cast<float>(getDeltaTime()));
        });
    }

    void update(float deltaTime) {
        if (!playing_ || completed_) return;

        // Consume delay first
        if (delayRemaining_ > 0.0f) {
            delayRemaining_ -= deltaTime;
            if (delayRemaining_ > 0.0f) return;
            deltaTime = -delayRemaining_;
            delayRemaining_ = 0.0f;
        }

        elapsed_ += deltaTime;

        if (elapsed_ >= duration_) {
            // Check if we should loop
            if (loopTotal_ == -1 || loopRemaining_ > 0) {
                // Carry over excess time
                float overflow = elapsed_ - duration_;
                elapsed_ = overflow;
                loopCount_++;

                if (loopTotal_ > 0) {
                    loopRemaining_--;
                }

                // Yoyo: toggle direction
                if (yoyo_) {
                    yoyoReversed_ = !yoyoReversed_;
                }

                // Re-apply delay for next loop iteration
                delayRemaining_ = delay_;
                return;
            }

            // No more loops — complete
            elapsed_ = duration_;
            playing_ = false;
            completed_ = true;
            updateListener_.disconnect();
            if (complete) complete->notify();
        }
    }

    float applyEasing(float t) const {
        if (asymmetric_) {
            return easeInOut(t, easeType_, easeTypeOut_);
        }
        return trussc::ease(t, easeType_, mode_);
    }

    // Generic lerp - works with float
    template<typename U = T>
    static typename std::enable_if<std::is_same<U, float>::value, U>::type
    lerpValue(U a, U b, float t) {
        return a + (b - a) * t;
    }

    // Generic lerp - works with types that have a lerp method (Vec2, Vec3, etc.)
    template<typename U = T>
    static typename std::enable_if<!std::is_same<U, float>::value, U>::type
    lerpValue(const U& a, const U& b, float t) {
        return a.lerp(b, t);
    }
};

} // namespace trussc
