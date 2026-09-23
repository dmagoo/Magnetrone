#include "midi.h"
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "audio/audio.h"
#include "sequencer/pitch.h"

// How far a full pitch bend travels on the receiver, in semitones. Announced
// via RPN 0 at init so both ends agree; the default in the MIDI spec is 2.
static uint8_t bendRangeSemis = 2;

void midiInit() {
    Serial1.begin(31250);
    midiSetBendRange(bendRangeSemis);
    midiSetBend(0.0f);
}

void midiUpdate() {
    // MIDI input handling goes here (future)
}

uint8_t midiNoteOn(uint8_t note, uint8_t velocity) {
    // Whole semitones of the global offset move the note number; the remainder
    // rides on the channel bend, which pitch.cpp keeps current.
    int shifted = (int)note + pitchNoteShift();
    uint8_t out = (uint8_t)constrain(shifted, 0, 127);

    Serial1.write(0x90 | (MIDI_CHANNEL - 1));
    Serial1.write(out & 0x7F);
    Serial1.write(velocity & 0x7F);

    // The internal synth has no such limitation: give it the exact frequency,
    // microtones and all, keyed to the same note number so Note Off matches.
    audioNoteOnFreq(out, velocity, pitchHz(note));
    return out;
}

void midiNoteOff(uint8_t emittedNote) {
    Serial1.write(0x80 | (MIDI_CHANNEL - 1));
    Serial1.write(emittedNote & 0x7F);
    Serial1.write(0x00);
    audioNoteOff(emittedNote);
}

// ---------------------------------------------------------------------------
// Pitch bend
// ---------------------------------------------------------------------------

void midiSetBend(float semitones) {
    if (bendRangeSemis == 0) return;

    // 14-bit, centre 8192, full scale 0..16383.
    float norm = semitones / (float)bendRangeSemis;      // -1.0 .. +1.0
    norm = constrain(norm, -1.0f, 1.0f);

    int value = 8192 + (int)lroundf(norm * 8191.0f);
    value = constrain(value, 0, 16383);

    Serial1.write(0xE0 | (MIDI_CHANNEL - 1));
    Serial1.write(value & 0x7F);           // LSB first
    Serial1.write((value >> 7) & 0x7F);    // then MSB
}

void midiSetBendRange(uint8_t semitones) {
    if (semitones == 0) return;
    bendRangeSemis = semitones;

    // RPN 0 = pitch bend sensitivity. Select the parameter, set it, then park
    // the RPN at "null" (127/127) so a later stray Data Entry cannot land on it.
    uint8_t cc = 0xB0 | (MIDI_CHANNEL - 1);
    Serial1.write(cc); Serial1.write(101); Serial1.write(0);          // RPN MSB
    Serial1.write(cc); Serial1.write(100); Serial1.write(0);          // RPN LSB
    Serial1.write(cc); Serial1.write(6);   Serial1.write(semitones);  // data MSB: semitones
    Serial1.write(cc); Serial1.write(38);  Serial1.write(0);          // data LSB: cents
    Serial1.write(cc); Serial1.write(101); Serial1.write(127);        // RPN null
    Serial1.write(cc); Serial1.write(100); Serial1.write(127);
}

// ---------------------------------------------------------------------------
// Beat clock and transport
//
// MIDI beat clock is 24 ticks per quarter note, so the tick interval is
//   60e6 us / (BPM * 24)  =  2.5e6 / BPM microseconds.
//
// Ticks are scheduled against a running deadline rather than "now + interval",
// so a late loop iteration does not accumulate drift. If the loop stalls long
// enough to miss several ticks the backlog is dropped rather than burst out,
// which keeps a downstream sequencer from lurching.
// ---------------------------------------------------------------------------

constexpr uint8_t MIDI_CLOCK    = 0xF8;
constexpr uint8_t MIDI_START    = 0xFA;
constexpr uint8_t MIDI_CONTINUE = 0xFB;
constexpr uint8_t MIDI_STOP     = 0xFC;

constexpr uint8_t  MIDI_CLOCK_PPQN = 24;
constexpr uint32_t MAX_TICK_BACKLOG = 4;   // ticks; beyond this, resynchronise

static bool     clockRunning   = false;
static bool     everStarted    = false;   // Start on the first run, Continue after
static uint32_t nextTickUs     = 0;

void midiStart()    { Serial1.write(MIDI_START); }
void midiStop()     { Serial1.write(MIDI_STOP); }
void midiContinue() { Serial1.write(MIDI_CONTINUE); }

void midiClockUpdate(float bpm, bool running) {
    // A stopped or nonsensical tempo means no clock. Guard the division.
    if (!running || bpm <= 0.0f) {
        if (clockRunning) {
            midiStop();
            clockRunning = false;
        }
        return;
    }

    uint32_t now         = micros();
    uint32_t intervalUs  = (uint32_t)(2500000.0f / bpm);
    if (intervalUs == 0) return;   // absurd tempo; nothing sensible to send

    if (!clockRunning) {
        if (everStarted) midiContinue();
        else             { midiStart(); everStarted = true; }
        clockRunning = true;
        nextTickUs   = now + intervalUs;
        return;
    }

    // Unsigned wrap-safe comparison: this stays correct across the micros()
    // rollover every ~71 minutes.
    while ((int32_t)(now - nextTickUs) >= 0) {
        Serial1.write(MIDI_CLOCK);
        nextTickUs += intervalUs;

        // If we are more than a few ticks behind (a long blocking call, say),
        // give up on the backlog and re-anchor to now.
        if ((int32_t)(now - nextTickUs) > (int32_t)(intervalUs * MAX_TICK_BACKLOG)) {
            nextTickUs = now + intervalUs;
            break;
        }
    }
}
