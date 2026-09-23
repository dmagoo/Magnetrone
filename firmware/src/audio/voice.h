#pragma once
#include <stdint.h>

// A voice is how a note sounds: the oscillator's wave shape, the envelope that
// shapes its loudness over time (ADSR), and how long the sequencer holds the
// note before sending Note Off.
//
// Note length belongs to the voice rather than being global because the
// envelopes differ so much: a slow Strings swell needs far longer than a
// Piano tap, and a fixed 250 ms would cut it off before it was heard.
struct Voice {
    const char* name;       // menu label
    short       waveform;   // WAVEFORM_* from the Teensy Audio library
    uint16_t    attackMs;
    uint16_t    decayMs;
    float       sustain;    // level, 0.0 - 1.0
    uint16_t    releaseMs;
    uint16_t    noteMs;     // Note On to Note Off
};

// Order matches VOICES[] in voice.cpp, and the stored cfg.voice is an index
// into it, so append new voices at the end or saved settings shift.
enum class VoiceId : uint8_t { Piano, Strings, Leads, Bass, COUNT };
constexpr uint8_t VOICE_COUNT = (uint8_t)VoiceId::COUNT;

// Out-of-range ids (a corrupt or future EEPROM value) fall back to Piano.
const Voice& voiceGet(uint8_t id);
