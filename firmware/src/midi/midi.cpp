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
