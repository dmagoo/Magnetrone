#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Global pitch offset.
//
// The Pitch aux function shifts the whole instrument by a signed number of
// semitones, and that shift does not have to be a whole number -- cfg.pitchStep
// can be a fraction of a semitone, which is the point of it (deliberately alien
// tunings between the normal notes).
//
// Two consumers, with different needs:
//
//   * The internal synth takes a float frequency, so it can play the offset
//     exactly. pitchHz() gives it that.
//   * MIDI note numbers are integers. A fractional offset has to go out as the
//     nearest note number plus a pitch bend for the remainder.
//
// This is live performance state, not configuration: the offset lives in RAM
// and is never written to EEPROM. Only the step size is saved.
//
// Offset is deliberately NOT clamped to the bend range. It splits as
// round-to-nearest note plus a remainder in [-0.5, +0.5] semitones, which
// always sits inside the default +/-2 semitone bend range, so the bend never
// clips no matter how far the offset travels.
// ---------------------------------------------------------------------------

// The incoming pitch bend wheel: a temporary offset on top of the Pitch
// offset, springing back to 0 at centre. Not part of pitchGetOffset(), so it
// never ends up in a scene.
void  pitchSetWheel(float semitones);

void  pitchSetOffset(float semitones);
void  pitchAdjust(float deltaSemitones);
float pitchGetOffset();

// Whole-semitone part of the current offset, to be added to a MIDI note number.
int8_t pitchNoteShift();

// Leftover fraction after pitchNoteShift(), in semitones, always [-0.5, +0.5].
// This is what goes out as pitch bend.
float pitchBendSemitones();

// Exact frequency for a base note under the current offset, for the internal
// synth. Bypasses the integer/bend split entirely.
float pitchHz(int baseNote);
