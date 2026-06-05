#pragma once
#include <stdint.h>

void audioInit(float volume, bool muted);
void audioSetVolume(float volume);  // 0.0 - 1.0
void audioMute();
void audioUnmute();

// Trigger a note on the internal synth. note is a MIDI note number (0-127).
void audioNoteOn(uint8_t note, uint8_t velocity);
void audioNoteOff(uint8_t note);
