#pragma once
#include <stdint.h>
#include "voice.h"
#include "config/storage.h"

void audioInit(float volume, bool muted);
void audioSetVolume(float volume);  // 0.0 - 1.0
void audioMute();
void audioUnmute();

// There is one bank of synth voices per magnet layer (A = 0, B = 1), so the
// two layers can play different voices at the same time.

// Applies a voice's wave shape and envelope to every synth voice in a layer's
// bank. Takes effect from the next note; notes already sounding keep their
// envelope timing. A kit voice leaves the bank untouched, since it plays the
// drum bank instead.
void audioSetVoice(uint8_t layer, const Voice& voice);

// Each bank has NUM_HALL_SENSORS voices. A new note takes a free one (its
// envelope finished), or steals the oldest when none is free.

// Trigger a note on a layer's bank at baseNote under the pitch offset
// (pitchHz()), so microtonal tunings reach the internal synth exactly --
// MIDI has to approximate them with a note number plus pitch bend, but this
// does not. It follows later pitch changes (audioRetune()). `note` is only
// the id used to match the later audioNoteOff().
void audioNoteOn(uint8_t layer, uint8_t note, uint8_t velocity, int baseNote);

// The same at a fixed frequency that pitch changes leave alone.
void audioNoteOnFreq(uint8_t layer, uint8_t note, uint8_t velocity, float hz);

// Releases the notes with this id on this layer's bank. Matching on the layer
// as well as the note keeps the two banks independent: the same note number
// playing on both is two different notes.
void audioNoteOff(uint8_t layer, uint8_t note);

// Keys played on the layer's bank (MIDI Fn Play Along), at the key's own
// note under the pitch offset. Their ids are separate from audioNoteOn()'s,
// so a key and a magnet on the same note sound together and neither's off
// cuts the other.
void audioKeyOn(uint8_t layer, uint8_t note, uint8_t velocity);
void audioKeyOff(uint8_t layer, uint8_t note);
void audioKeysOff();   // every key on both banks

// Retunes every sounding note to the current pitch offset and bend wheel.
// pitch.cpp calls it whenever either moves.
void audioRetune();

// Sets a layer's effects chain: Tone, Chorus, Delay, Reverb. Pass the
// settings the layer plays with, Same as A already resolved (layerFx()).
void audioSetEffects(uint8_t layer, const LayerFx& fx);

// The longest delay each layer's buffer holds.
constexpr uint16_t DELAY_MAX_MS = 2400;

// The layer's delay time, in ms. Separate from audioSetEffects(), since a
// Sync time follows the platter speed.
void audioSetDelayTime(uint8_t layer, float ms);

// Which layer's effects the shared drum kit goes through.
void audioSetDrumLayer(uint8_t layer);

// Fires one drum of the shared kit (slot = DrumSlot, see kit.h). Drums are
// one-shots with no matching off. A closed hat chokes the open hat.
void audioDrumHit(uint8_t slot, uint8_t velocity);
