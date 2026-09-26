#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// MIDI in -- control only. Incoming notes never play the table; they drive
// the MIDI Fn, the way the Aux knob drives the Aux Fn.
//
// Each layer listens on its own channel (cfg.midiInChannel). A message on a
// layer's channel applies to that layer; with both on one channel it applies
// to both. Shared settings (root, scale, octave, pitch, volume) take a
// message from either layer's channel.
//
//   Keys           the MIDI Fn: Pitch, Shift, Scale Learn or Chord (or Off)
//   Pitch bend     always a temporary pitch offset, +/-MIDI_BEND_IN_RANGE
//   CC 7           volume
//   CC 20          octave
//
// Everything here is live, like the Aux knob: nothing is saved except the
// volume (as the volume knob does) and, through scenes, a learned scale.
//
// If the other end echoes the table's MIDI out back to its input, the table's
// own notes arrive here as control. Echo (MIDI thru) must be off there.
// =============================================================================

// Stored in cfg.midiFn: new Fns go at the end.
enum class MidiFn : uint8_t { Off, Pitch, Shift, ScaleLearn, Chord, COUNT };

void midiInUpdate(SavedConfig& cfg);   // call every loop

// Forget any partly collected Scale Learn keys (the MIDI Fn changed).
void midiInReset();

// True once after an incoming message changed something on the live display.
bool midiInTakeChanged();
