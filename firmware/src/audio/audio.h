#pragma once
#include <stdint.h>
#include "voice.h"

void audioInit(float volume, bool muted);
void audioSetVolume(float volume);  // 0.0 - 1.0
void audioMute();
void audioUnmute();

// There is one bank of synth voices per magnet layer (A = 0, B = 1), so the
// two layers can play different voices at the same time.

// Applies a voice's wave shape and envelope to every synth voice in a layer's
// bank. Takes effect from the next note; notes already sounding keep their
// envelope timing.
void audioSetVoice(uint8_t layer, const Voice& voice);

// Trigger a note on a layer's bank at an explicit frequency. The oscillator
// takes a float, so microtonal tunings reach the internal synth exactly --
// MIDI has to approximate them with a note number plus pitch bend, but this
// does not. `note` is only the id used to match the later audioNoteOff().
void audioNoteOnFreq(uint8_t layer, uint8_t note, uint8_t velocity, float hz);

// Releases the notes with this id on this layer's bank. Matching on the layer
// as well as the note keeps the two banks independent: the same note number
// playing on both is two different notes.
void audioNoteOff(uint8_t layer, uint8_t note);
