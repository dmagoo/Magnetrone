#pragma once
#include <stdint.h>

void midiInit();   // MIDI in is midi_in.h

// Sends Note On on `channel` (1-16) for `note` shifted by the current global
// pitch offset, and fires the given layer's internal synth bank at the exact
// (possibly microtonal) frequency. A note that lands outside 0-127 is folded
// by octaves into range, for MIDI and the internal synth alike.
// RETURNS the MIDI note number actually emitted -- pass that to midiNoteOff().
// Do not re-derive it later: the pitch offset may have moved in between, and
// the Note Off has to match the Note On that was actually sent. The same goes
// for the channel and layer, which can change mid-note from the menu.
uint8_t midiNoteOn(uint8_t layer, uint8_t channel, uint8_t note, uint8_t velocity);

// Takes the note number midiNoteOn() returned, not the pre-shift note, and the
// layer and channel it was sent with.
void midiNoteOff(uint8_t layer, uint8_t channel, uint8_t emittedNote);

// Drum hit for a kit voice: sends the slot's fixed GM note on `channel` and
// fires that drum on the internal kit. Bypasses the pitch offset and bend.
// RETURNS the note sent, for midiDrumOff().
uint8_t midiDrumOn(uint8_t channel, uint8_t slot, uint8_t velocity);
void    midiDrumOff(uint8_t channel, uint8_t note);

// Tells MIDI which channels the two layers currently play on (1-16, or 0 for
// a layer that is off or plays a kit, since drums take no bend). Pitch bend
// and bend range go to each of them, and a
// channel newly in use is sent the bend range and current bend straight away.
void midiSetLayerChannels(uint8_t channelA, uint8_t channelB);

// --- Pitch bend --------------------------------------------------------------
// Bend is per-CHANNEL, not per-note. That is fine here because the Pitch
// function shifts the whole instrument, so every sounding note shares one
// offset; the same bend goes to every layer channel in use. It would NOT be
// fine if tracks ever got individual detuning.
void midiSetBend(float semitones);

// Tells the receivers how far a full bend travels, via RPN 0. Receivers
// default to +/-2 semitones but not all of them, and the bend maths has to
// know.
void midiSetBendRange(uint8_t semitones);

// --- Beat clock / transport -------------------------------------------------
// Raw messages. transport.cpp decides when to send them, from the platter.
void midiClock();                            // 0xF8, one of 24 per quarter note
void midiStart();
void midiStop();
void midiContinue();
void midiSongPosition(uint16_t sixteenths);  // Song Position Pointer
