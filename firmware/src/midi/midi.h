#pragma once
#include <stdint.h>

void midiInit();
void midiUpdate();
void midiNoteOn(uint8_t note, uint8_t velocity);
void midiNoteOff(uint8_t note);

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
