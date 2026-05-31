#pragma once

// =============================================================================
// tcxMidi - TrussC MIDI addon
// =============================================================================
// MIDI input/output for TrussC, backed by libremidi (CoreMIDI / WinMM /
// ALSA / JACK / WebMIDI). Include this single header to use everything.
//
//   #include <tcxMidi.h>
//   using namespace tcx;
//
//   MidiIn  in;
//   MidiOut out;
//   in.openPort(0);
//   in.onMessage.listen([](MidiMessage& m){ logNotice() << m.toString(); });
//   out.openPort(0);
//   out.sendNoteOn(1, 60, 100);
//
// See README.md for examples and thread-safety notes.
// =============================================================================

#include "tcxMidiConstants.h"
#include "tcxMidiMessage.h"
#include "tcxMidiIn.h"
#include "tcxMidiOut.h"
#include "tcxMidiClock.h"
#include "tcxMidiTimecode.h"
