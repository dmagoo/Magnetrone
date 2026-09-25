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
// Beat clock and transport messages. When to send them is transport.cpp's job.
// ---------------------------------------------------------------------------

constexpr uint8_t MIDI_CLOCK    = 0xF8;
constexpr uint8_t MIDI_START    = 0xFA;
constexpr uint8_t MIDI_CONTINUE = 0xFB;
constexpr uint8_t MIDI_STOP     = 0xFC;
constexpr uint8_t MIDI_SPP      = 0xF2;

void midiClock()    { Serial1.write(MIDI_CLOCK); }
void midiStart()    { Serial1.write(MIDI_START); }
void midiStop()     { Serial1.write(MIDI_STOP); }
void midiContinue() { Serial1.write(MIDI_CONTINUE); }

void midiSongPosition(uint16_t sixteenths) {
    sixteenths &= 0x3FFF;                  // 14 bits
    Serial1.write(MIDI_SPP);
    Serial1.write(sixteenths & 0x7F);      // LSB first
    Serial1.write((sixteenths >> 7) & 0x7F);
}
