#pragma once
#include <stdint.h>

void midiInit();
void midiUpdate();
void midiNoteOn(uint8_t note, uint8_t velocity);
void midiNoteOff(uint8_t note);
