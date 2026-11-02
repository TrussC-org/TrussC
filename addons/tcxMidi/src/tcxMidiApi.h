#pragma once

// =============================================================================
// tcxMidiApi.h - pick the platform's native libremidi backend
// =============================================================================
// libremidi's default (no-API) observer/midi_in/midi_out picks the FIRST
// backend in its compiled list. On some platforms (notably Android) that first
// entry is the "Computer keyboard" backend, so device enumeration silently
// returns nothing. We therefore pin the platform's real MIDI API explicitly.
//
// libremidi::midi1::default_api() returns the right API for macOS / Windows /
// Linux / Android, but on Emscripten its internal check uses the wrong macro
// (`__emscripten__` instead of `__EMSCRIPTEN__`) and falls through to DUMMY, so
// we special-case the web here.
// =============================================================================

#include <libremidi/libremidi.hpp>

#include "tc/utils/tcMainThread.h"

#include <atomic>
#include <functional>
#include <memory>

namespace tcx::midi {

using namespace tc;

inline libremidi::API platformMidiApi() {
#if defined(__EMSCRIPTEN__)
    return libremidi::API::WEBMIDI;
#else
    return libremidi::midi1::default_api();
#endif
}

namespace internal {
inline libremidi::observer_configuration midiObserverConfiguration() {
    libremidi::observer_configuration config;
    config.track_virtual = true;
    return config;
}

// Tells MidiIn / MidiOut that some port of one direction was removed. It does
// not say which: the owner looks at the port list again, so it does not depend
// on what a backend can still read from a port that is already gone. The
// callback runs on the main thread at the next host turn (never inside the
// backend's own callback, so it may close the port and destroy this watch),
// at most once per turn, and never after the watch is destroyed.
//
// libremidi's observer reports a removal on CoreMIDI (kMIDIMsgObjectRemoved),
// ALSA sequencer (port exit) and WinMM (a 100 ms poll of the device list). The
// WebMIDI backend never reports one, so there is no watch there.
#if !defined(__EMSCRIPTEN__)
class PortWatch {
public:
    PortWatch(bool input, std::function<void()> onPortRemoved)
        : shared_(std::make_shared<Shared>(std::move(onPortRemoved))) {
        // notify_in_constructor stays on: the ALSA observer only reports a
        // removal for ports it announced to its (here empty) added-callbacks.
        // The other callbacks are set because some backends call them unchecked.
        auto config = midiObserverConfiguration();
        config.input_added = [](const libremidi::input_port&) {};
        config.output_added = [](const libremidi::output_port&) {};
        auto removed = [shared = shared_]() { shared->post(shared); };
        if (input) {
            config.input_removed = [removed](const libremidi::input_port&) { removed(); };
            config.output_removed = [](const libremidi::output_port&) {};
        } else {
            config.input_removed = [](const libremidi::input_port&) {};
            config.output_removed = [removed](const libremidi::output_port&) { removed(); };
        }
        observer_ = std::make_unique<libremidi::observer>(
            std::move(config), libremidi::observer_configuration_for(platformMidiApi()));
    }
    ~PortWatch() {
        shared_->alive.store(false);   // a delivery already queued becomes a no-op
        observer_.reset();             // joins the backend's thread
    }
    PortWatch(const PortWatch&) = delete;
    PortWatch& operator=(const PortWatch&) = delete;

private:
    struct Shared : trussc::internal::MainThreadSlot {
        std::function<void()> onPortRemoved;
        std::atomic<bool> alive{true};
        explicit Shared(std::function<void()> fn)
            : MainThreadSlot(&deliver), onPortRemoved(std::move(fn)) {}
        static void deliver(MainThreadSlot& slot) {
            auto& self = static_cast<Shared&>(slot);
            if (self.alive.load()) self.onPortRemoved();
        }
    };
    std::shared_ptr<Shared> shared_;
    std::unique_ptr<libremidi::observer> observer_;
};
#endif
}  // namespace internal

}  // namespace tcx::midi
