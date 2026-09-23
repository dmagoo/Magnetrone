#pragma once
#include <stdint.h>

void audioInit(float volume, bool muted);
void audioSetVolume(float volume);  // 0.0 - 1.0
void audioMute();
void audioUnmute();

// Trigger a note on the internal synth. note is a MIDI note number (0-127).
void audioNoteOn(uint8_t note, uint8_t velocity);
void audioNoteOff(uint8_t note);

// As audioNoteOn(), but plays an explicit frequency instead of deriving it from
// the note number. The oscillator takes a float, so this is how microtonal
// tunings reach the internal synth exactly -- MIDI has to approximate them with
// a note number plus pitch bend, but this does not. `note` is still the id used
// to match the later audioNoteOff().
void audioNoteOnFreq(uint8_t note, uint8_t velocity, float hz);
