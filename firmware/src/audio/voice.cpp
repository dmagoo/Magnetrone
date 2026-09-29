#include "voice.h"
#include <Audio.h>
#include <math.h>

// Starting values, agreed 2026-09-23 and expected to be tuned by ear. The
// bandlimited saw and square alias far less than the plain ones, which matters
// on the high octaves. Auto channels: Piano 1, Bass 2, Strings 3, Leads 4; Drums
// take 10, per General MIDI. Drums ignore waveform and envelope (the drum bank has
// its own per-drum sounds); its note length only times the MIDI Note Off.
// Every built-in starts with its filter Off. The envelope values are where
// Voice Edit starts from once the cutoff comes down.
static constexpr VoiceFilter FILTER_OFF = { VOICE_FILTER_OFF, 0, 0, 50, 5, 300, 300 };

static const Voice VOICES[VOICE_COUNT] = {
    //  name       waveform                      A    D    S      R    note  ch  source              harmonics   filter
    { "Piano",   WAVEFORM_TRIANGLE,             5, 400, 0.2f, 400, 250,  1, NoteSource::Scale,  {}, false, FILTER_OFF },
    { "Strings", WAVEFORM_BANDLIMIT_SAWTOOTH, 150, 100, 0.8f, 600, 800,  3, NoteSource::Scale,  {}, false, FILTER_OFF },
    { "Leads",   WAVEFORM_BANDLIMIT_SQUARE,     5, 100, 0.7f, 150, 200,  4, NoteSource::Scale,  {}, false, FILTER_OFF },
    { "Bass",    WAVEFORM_BANDLIMIT_SAWTOOTH,   5, 150, 0.5f, 100, 250,  2, NoteSource::Scale,  {}, false, FILTER_OFF },
    { "Drums",   WAVEFORM_SINE,                 0,   0, 0.0f,   0,  50, 10, NoteSource::Kit,    {}, false, FILTER_OFF },
    { "None",    WAVEFORM_SINE,                 0,   0, 0.0f,   0,   0,  1, NoteSource::Silent, {}, false, FILTER_OFF },
};

const Voice& voiceGet(uint8_t id) {
    return VOICES[(id < VOICE_COUNT) ? id : (uint8_t)VoiceId::Piano];
}

static const short WAVEFORMS[WAVE_COUNT] = {
    WAVEFORM_SINE, WAVEFORM_TRIANGLE, WAVEFORM_BANDLIMIT_SAWTOOTH, WAVEFORM_BANDLIMIT_SQUARE
};
static const char* WAVE_NAMES[WAVE_COUNT] = { "Sine", "Triangle", "Saw", "Square" };

short voiceWaveform(Wave w) {
    return WAVEFORMS[(uint8_t)w < WAVE_COUNT ? (uint8_t)w : 0];
}

Wave voiceWave(short waveform) {
    if (waveform == WAVEFORM_SAWTOOTH) return Wave::Saw;
    if (waveform == WAVEFORM_SQUARE)   return Wave::Square;
    for (uint8_t i = 0; i < WAVE_COUNT; i++) if (WAVEFORMS[i] == waveform) return (Wave)i;
    return Wave::Sine;
}

const char* voiceWaveName(Wave w) {
    return WAVE_NAMES[(uint8_t)w < WAVE_COUNT ? (uint8_t)w : 0];
}

// Saw has every harmonic at 1/n, Square the odd ones at 1/n, Triangle the odd
// ones at 1/n^2. Triangle's alternate harmonics are phase-flipped, which a
// level cannot say, but a steady tone sounds the same either way.
void voiceHarmonicsFrom(Wave w, uint8_t harmonics[NUM_HARMONICS]) {
    for (uint8_t i = 0; i < NUM_HARMONICS; i++) {
        uint8_t n = i + 1;
        bool    odd = (n % 2) == 1;
        float   level = 0.0f;
        switch (w) {
            case Wave::Sine:     level = (n == 1) ? 1.0f : 0.0f;          break;
            case Wave::Triangle: level = odd ? 1.0f / (n * n) : 0.0f;     break;
            case Wave::Saw:      level = 1.0f / n;                        break;
            case Wave::Square:   level = odd ? 1.0f / n : 0.0f;           break;
            default: break;
        }
        harmonics[i] = (uint8_t)lroundf(level * 100.0f);
    }
}
