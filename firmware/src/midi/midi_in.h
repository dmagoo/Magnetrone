#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// MIDI in -- control only. Incoming notes never play the table; they drive
// the MIDI Fn, the way the Aux knob drives the Aux Fn.
//
// Each layer listens on its own channel (cfg.midiInChannel). A message on a
// layer's channel applies to that layer; with both on one channel it applies
// to both. Shared settings (pitch, volume) take a message from either
// layer's channel. Layer B in Same as A does not listen: it plays A's.
//
//   Keys           the MIDI Fn: Pitch, Shift, Scale Learn, Set Scale,
//                  Fingered or One Finger (or Off)
//   Pitch bend     always a temporary pitch offset, +/-MIDI_BEND_IN_RANGE
//   CC 7           volume
//   CC 20          octave
//   CC 74, 71      Tone Cutoff, Resonance    } only with Play Setup > MIDI CC
//   CC 93          Chorus Mix                } On; 0-127 onto 0-100%, so
//   CC 91          Reverb Mix                } 127 is Cutoff Off
//   CC 94          Delay Mix                 }
//   CC 12          Delay Feedback            } 0-127 onto 0-90%, the cap
//
// Everything here is live, like the Aux knob: nothing is saved except the
// volume (as the volume knob does). Save Scene keeps the rest; Scale Learn,
// Fingered and One Finger write the layer's Custom scale, which scenes keep
// as Edit Scale's.
//
// If the other end echoes the table's MIDI out back to its input, the table's
// own notes arrive here as control. Echo (MIDI thru) must be off there.
// =============================================================================

// Stored in cfg.midiFn: new Fns go at the end.
enum class MidiFn : uint8_t { Off, Pitch, Shift, ScaleLearn, SetScale, Fingered, OneFinger, COUNT };

void midiInUpdate(SavedConfig& cfg);   // call every loop

// Forget any partly collected chord and held keys (the MIDI Fn changed).
void midiInReset();

// True once after an incoming message changed something on the live display.
bool midiInTakeChanged();
