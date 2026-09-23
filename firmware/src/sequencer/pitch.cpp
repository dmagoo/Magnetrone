#include "pitch.h"
#include <Arduino.h>
#include <math.h>
#include "midi/midi.h"

// Live modulation state. RAM only, by design -- see pitch.h.
static float offsetSemis = 0.0f;

// Keep the outgoing bend in step with the offset. Every note sounding on the
// channel shares this one bend value, which is correct here precisely because
// the Pitch function shifts the whole instrument rather than one note.
static void publishBend() {
    midiSetBend(pitchBendSemitones());
}

void pitchSetOffset(float semitones) {
    offsetSemis = semitones;
    publishBend();
}

void pitchAdjust(float deltaSemitones) {
    offsetSemis += deltaSemitones;
    publishBend();
}

float pitchGetOffset() {
    return offsetSemis;
}

int8_t pitchNoteShift() {
    float r = roundf(offsetSemis);
    // A note number is 0-127, so anything beyond an octave or two of shift is
    // already musically absurd; clamping keeps the int8_t honest.
    r = constrain(r, -127.0f, 127.0f);
    return (int8_t)r;
}

float pitchBendSemitones() {
    return offsetSemis - (float)pitchNoteShift();
}

float pitchHz(int baseNote) {
    // A4 = note 69 = 440 Hz. The offset is applied before the conversion, so
    // the internal synth hears the exact tuning with no rounding at all.
    return 440.0f * powf(2.0f, ((float)baseNote + offsetSemis - 69.0f) / 12.0f);
}
