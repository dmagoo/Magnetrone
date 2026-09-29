#pragma once
#include <stdint.h>

// A voice is how a note sounds: the oscillator's wave shape, the envelope that
// shapes its loudness over time (ADSR), and how long the sequencer holds the
// note before sending Note Off.
//
// Note length belongs to the voice rather than being global because the
// envelopes differ so much: a slow Strings swell needs far longer than a
// Piano tap, and a fixed 250 ms would cut it off before it was heard.
//
// A voice's note source says where its notes come from. Scale voices play the
// layer's root/scale/octave on the synth bank. Kit voices play a fixed drum per
// sensor on the drum bank and ignore the waveform and envelope. Code that
// treats drums differently checks the source, not VoiceId::Drums, so a second
// kit added later behaves the same without touching it. A Silent voice plays
// nothing at all: it mutes a layer, live from the Aux, without switching it Off.
enum class NoteSource : uint8_t { Scale, Kit, Silent };

// Voice Edit can reshape any wave by the levels of its first 16 harmonics,
// each 0-100%. Until one is edited the voice plays its stock wave and the
// levels are unused; the built-ins leave them at 0.
constexpr uint8_t NUM_HARMONICS = 16;

struct Voice {
    const char* name;       // menu label
    short       waveform;   // WAVEFORM_* from the Teensy Audio library
    uint16_t    attackMs;
    uint16_t    decayMs;
    float       sustain;    // level, 0.0 - 1.0
    uint16_t    releaseMs;
    uint16_t    noteMs;     // Note On to Note Off
    uint8_t     autoChannel; // MIDI channel when a layer's channel is Auto
    NoteSource  source;
    uint8_t     harmonics[NUM_HARMONICS];   // percent; used only when harmonicsEdited
    bool        harmonicsEdited;            // plays the wave built from harmonics[]
};

// Order matches VOICES[] in voice.cpp, and the stored cfg.layer[].voice is an index
// into it, so append new voices at the end or saved settings shift.
enum class VoiceId : uint8_t { Piano, Strings, Leads, Bass, Drums, None, COUNT };
constexpr uint8_t VOICE_COUNT = (uint8_t)VoiceId::COUNT;

// Out-of-range ids (a corrupt or future EEPROM value) fall back to Piano.
const Voice& voiceGet(uint8_t id);

// Saved voices, stored by id beside the built-ins in LayerCfg::voice. The
// ids leave room for more built-ins below them.
constexpr uint8_t NUM_CUSTOM_VOICES  = 8;
constexpr uint8_t VOICE_CUSTOM_FIRST = 16;   // 16-23: Custom 1-8, shared by all scenes
constexpr uint8_t VOICE_SCENE        = 32;   // the current scene's own voice for the layer

inline bool voiceIsCustomId(uint8_t id) {
    return id >= VOICE_CUSTOM_FIRST && id < VOICE_CUSTOM_FIRST + NUM_CUSTOM_VOICES;
}

// The wave shapes Voice Edit offers. Saw and Square are the band-limited
// ones the presets use.
enum class Wave : uint8_t { Sine, Triangle, Saw, Square, COUNT };
constexpr uint8_t WAVE_COUNT = (uint8_t)Wave::COUNT;
short       voiceWaveform(Wave w);        // WAVEFORM_* for Voice::waveform
Wave        voiceWave(short waveform);    // the reverse; unknown shapes read as Sine
const char* voiceWaveName(Wave w);

// The harmonic levels that come closest to a stock wave: where editing a
// wave's harmonics starts from.
void        voiceHarmonicsFrom(Wave w, uint8_t harmonics[NUM_HARMONICS]);

inline bool voiceIsKit(const Voice& v)    { return v.source == NoteSource::Kit; }
inline bool voiceIsSilent(const Voice& v) { return v.source == NoteSource::Silent; }
