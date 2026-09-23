#pragma once
#include <stdint.h>

void midiInit();
void midiUpdate();
// Sends Note On for `note` shifted by the current global pitch offset, and
// fires the internal synth at the exact (possibly microtonal) frequency.
// RETURNS the MIDI note number actually emitted -- pass that to midiNoteOff().
// Do not re-derive it later: the pitch offset may have moved in between, and
// the Note Off has to match the Note On that was actually sent.
uint8_t midiNoteOn(uint8_t note, uint8_t velocity);

// Takes the note number midiNoteOn() returned, not the pre-shift note.
void midiNoteOff(uint8_t emittedNote);

// --- Pitch bend --------------------------------------------------------------
// Bend is per-CHANNEL, not per-note. That is fine here because the Pitch
// function shifts the whole instrument, so every sounding note shares one
// offset. It would NOT be fine if tracks ever got individual detuning.
void midiSetBend(float semitones);

// Tells the receiver how far a full bend travels, via RPN 0. Receivers default
// to +/-2 semitones but not all of them, and the bend maths has to know.
void midiSetBendRange(uint8_t semitones);

// --- Beat clock / transport -------------------------------------------------
// Drives MIDI beat clock (0xF8, 24 ppqn) plus Start / Stop / Continue from the
// platter, so external gear follows the table's tempo. Call every loop with the
// current platter BPM and whether the platter is turning; it emits Start on the
// first spin-up, Continue on later ones, and Stop when the platter halts.
void midiClockUpdate(float bpm, bool running);

// Individual transport messages, if something needs to send them directly.
void midiStart();
void midiStop();
void midiContinue();
