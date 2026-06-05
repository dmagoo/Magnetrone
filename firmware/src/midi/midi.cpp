#include "midi.h"
#include <Arduino.h>
#include "config.h"
#include "audio/audio.h"

void midiInit() {
    Serial1.begin(31250);
}

void midiUpdate() {
    // MIDI input handling goes here (future)
}

void midiNoteOn(uint8_t note, uint8_t velocity) {
    Serial1.write(0x90 | (MIDI_CHANNEL - 1));
    Serial1.write(note & 0x7F);
    Serial1.write(velocity & 0x7F);
    audioNoteOn(note, velocity);
}

void midiNoteOff(uint8_t note) {
    Serial1.write(0x80 | (MIDI_CHANNEL - 1));
    Serial1.write(note & 0x7F);
    Serial1.write(0x00);
    audioNoteOff(note);
}
