#include "midi.h"
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "audio/audio.h"
#include "audio/kit.h"
#include "sequencer/pitch.h"

// How far a full pitch bend travels on the receiver, in semitones. Announced
// via RPN 0 so both ends agree; the default in the MIDI spec is 2.
static uint8_t bendRangeSemis = 2;
static float   bendSemis      = 0.0f;   // last bend sent, for newly used channels

// The channels the layers play on, 1-16, or 0 for none.
static uint8_t layerChannel[2] = { 0, 0 };

void midiInit() {
    Serial1.begin(31250);
    // Nothing is sent until midiSetLayerChannels() says which channels are in
    // use; it sends the bend range and bend to each one.
}

void midiUpdate() {
    // MIDI input handling goes here (future)
}

uint8_t midiNoteOn(uint8_t layer, uint8_t channel, uint8_t note, uint8_t velocity) {
    // Whole semitones of the global offset move the note number; the remainder
    // rides on the channel bend, which pitch.cpp keeps current.
    int base    = note;
    int shifted = base + pitchNoteShift();

    // Out of MIDI range (a high octave, a big pitch offset, or No Wrap's
    // octave carry): fold down, or up, by whole octaves. The base note folds
    // with it so the internal synth plays the same note MIDI sends. Clamping
    // instead would pile every such note onto 127.
    while (shifted > 127) { shifted -= 12; base -= 12; }
    while (shifted < 0)   { shifted += 12; base += 12; }
    uint8_t out = (uint8_t)shifted;

    if (channel >= 1 && channel <= 16) {
        Serial1.write(0x90 | (channel - 1));
        Serial1.write(out & 0x7F);
        Serial1.write(velocity & 0x7F);
    }

    // The internal synth has no such limitation: give it the exact frequency,
    // microtones and all, keyed to the same note number so Note Off matches.
    audioNoteOnFreq(layer, out, velocity, pitchHz(base));
    return out;
}

void midiNoteOff(uint8_t layer, uint8_t channel, uint8_t emittedNote) {
    if (channel >= 1 && channel <= 16) {
        Serial1.write(0x80 | (channel - 1));
        Serial1.write(emittedNote & 0x7F);
        Serial1.write(0x00);
    }
    audioNoteOff(layer, emittedNote);
}

uint8_t midiDrumOn(uint8_t channel, uint8_t slot, uint8_t velocity) {
    if (slot >= NUM_HALL_SENSORS) return 0;
    // No pitch offset: shifting a GM drum number changes which drum plays.
    uint8_t note = KIT_GM_NOTE[slot];
    if (channel >= 1 && channel <= 16) {
        Serial1.write(0x90 | (channel - 1));
        Serial1.write(note);
        Serial1.write(velocity & 0x7F);
    }
    audioDrumHit(slot, velocity);
    return note;
}

void midiDrumOff(uint8_t channel, uint8_t note) {
    // MIDI only. Most receivers ignore Note Off on drums, but a well-formed
    // stream still pairs them. The internal drums are one-shots.
    if (channel >= 1 && channel <= 16) {
        Serial1.write(0x80 | (channel - 1));
        Serial1.write(note & 0x7F);
        Serial1.write(0x00);
    }
}

// ---------------------------------------------------------------------------
// Pitch bend
// ---------------------------------------------------------------------------

static void sendBend(uint8_t channel) {
    if (bendRangeSemis == 0) return;

    // 14-bit, centre 8192, full scale 0..16383.
    float norm = bendSemis / (float)bendRangeSemis;      // -1.0 .. +1.0
    norm = constrain(norm, -1.0f, 1.0f);

    int value = 8192 + (int)lroundf(norm * 8191.0f);
    value = constrain(value, 0, 16383);

    Serial1.write(0xE0 | (channel - 1));
    Serial1.write(value & 0x7F);           // LSB first
    Serial1.write((value >> 7) & 0x7F);    // then MSB
}

static void sendBendRange(uint8_t channel) {
    // RPN 0 = pitch bend sensitivity. Select the parameter, set it, then park
    // the RPN at "null" (127/127) so a later stray Data Entry cannot land on it.
    uint8_t cc = 0xB0 | (channel - 1);
    Serial1.write(cc); Serial1.write(101); Serial1.write(0);               // RPN MSB
    Serial1.write(cc); Serial1.write(100); Serial1.write(0);               // RPN LSB
    Serial1.write(cc); Serial1.write(6);   Serial1.write(bendRangeSemis);  // data MSB: semitones
    Serial1.write(cc); Serial1.write(38);  Serial1.write(0);               // data LSB: cents
    Serial1.write(cc); Serial1.write(101); Serial1.write(127);             // RPN null
    Serial1.write(cc); Serial1.write(100); Serial1.write(127);
}

// Calls fn once for each distinct layer channel in use. Both layers on the
// same channel (Same as A, say) is one receiver, so it is sent once.
template <typename F>
static void forEachChannel(F fn) {
    if (layerChannel[0]) fn(layerChannel[0]);
    if (layerChannel[1] && layerChannel[1] != layerChannel[0]) fn(layerChannel[1]);
}

void midiSetLayerChannels(uint8_t channelA, uint8_t channelB) {
    uint8_t next[2] = { channelA, channelB };
    for (uint8_t l = 0; l < 2; l++) {
        if (next[l] > 16) next[l] = 0;
        uint8_t ch = next[l];
        bool wasInUse = (ch == layerChannel[0] || ch == layerChannel[1]);
        bool dupe     = (l == 1 && ch == next[0]);
        if (ch && !wasInUse && !dupe) {
            sendBendRange(ch);
            sendBend(ch);
        }
    }
    layerChannel[0] = next[0];
    layerChannel[1] = next[1];
}

void midiSetBend(float semitones) {
    bendSemis = semitones;
    forEachChannel(sendBend);
}

void midiSetBendRange(uint8_t semitones) {
    if (semitones == 0) return;
    bendRangeSemis = semitones;
    forEachChannel(sendBendRange);
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
