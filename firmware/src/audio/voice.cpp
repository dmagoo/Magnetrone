#include "voice.h"
#include <Audio.h>
#include <math.h>

// The built-in voices, picked by ear on the Voice A/B page 2026-10-06 (each
// a Final there). A harmonic voice plays its harmonic levels; its waveform
// is only where Voice Edit's Wave starts. Filter: cutoff and resonance in
// percent, then Amount (how far its envelope opens it, 0 = fixed), then that
// envelope's sustain %, attack, decay and release ms. Auto channels: Piano 1,
// Bass 2, Strings 3, Synth 4, E. Piano 5, Organ 6, Brass 7, Mallets 8, Reed 9,
// Guitar 11; Drums take 10, per General MIDI (agreed 2026-10-06). Drums
// ignore waveform and envelope (the drum bank has its own per-drum sounds);
// its note length only times the MIDI Note Off.
static constexpr VoiceFilter FILTER_OFF = { VOICE_FILTER_OFF, 0, 0, 50, 5, 300, 300 };

static const Voice VOICES[VOICE_COUNT] = {
    //  name        waveform                     A     D    S      R    note  ch  source
    //  harmonics 1-16 (percent), edited, filter { cut, res, amount, sus, A, D, R }
    { "Piano",    WAVEFORM_SINE,                1, 1200, 0.00f, 300, 500,  1, NoteSource::Scale,
      { 100, 25, 22, 6, 8, 2, 2 }, true, { 65, 0, 35, 0, 0, 60, 200 } },
    { "Strings",  WAVEFORM_SINE,              300,    0, 1.00f, 600, 350,  3, NoteSource::Scale,
      { 100, 50, 33, 25, 20, 17, 14, 12 }, true, { 50, 0, 0, 50, 5, 300, 300 } },
    { "Synth",    WAVEFORM_SINE,                1,  280, 0.05f, 300, 450,  4, NoteSource::Scale,
      { 100, 20, 65, 10, 35, 5, 25, 5, 15 }, true, { 60, 5, 15, 5, 1, 180, 250 } },
    { "Bass",     WAVEFORM_BANDLIMIT_SAWTOOTH,  2,  300, 0.35f,  80, 220,  2, NoteSource::Scale,
      {}, false, { 30, 10, 20, 15, 0, 200, 80 } },
    { "Drums",    WAVEFORM_SINE,                0,    0, 0.00f,   0,  50, 10, NoteSource::Kit,
      {}, false, FILTER_OFF },
    { "None",     WAVEFORM_SINE,                0,    0, 0.00f,   0,   0,  1, NoteSource::Silent,
      {}, false, FILTER_OFF },
    { "E. Piano", WAVEFORM_SINE,                0, 1200, 0.10f, 350, 400,  5, NoteSource::Scale,
      { 100, 40, 8, 5, 8, 0, 6 }, true, { 40, 10, 35, 0, 0, 600, 400 } },
    { "Organ",    WAVEFORM_SINE,               40,    0, 1.00f, 120, 800,  6, NoteSource::Scale,
      { 100, 70, 50, 60, 0, 40, 0, 45, 0, 0, 0, 6, 0, 0, 0, 5 }, true, FILTER_OFF },
    { "Brass",    WAVEFORM_SINE,               60,  300, 0.70f, 100, 400,  7, NoteSource::Scale,
      { 100, 90, 80, 70, 55, 40, 30, 20, 12, 8 }, true, { 35, 15, 60, 30, 80, 250, 100 } },
    { "Mallets",  WAVEFORM_TRIANGLE,            1,  500, 0.00f, 200, 400,  8, NoteSource::Scale,
      {}, false, { 55, 0, 35, 0, 0, 60, 200 } },
    { "Reed",     WAVEFORM_SINE,               40,  100, 0.85f, 150, 1200, 9, NoteSource::Scale,
      { 100, 0, 45, 0, 25, 0, 14, 0, 8, 0, 4 }, true, { 45, 10, 0, 50, 5, 300, 300 } },
    { "Guitar",   WAVEFORM_SINE,                0,  800, 0.00f, 300, 500, 11, NoteSource::Scale,
      { 100, 80, 60, 40, 20 }, true, { 30, 10, 45, 0, 0, 180, 300 } },
};

// The built-ins in menu order, which differs from the stored order (ids are
// appended so saved settings keep theirs).
const uint8_t VOICE_MENU_ORDER[VOICE_COUNT] = {
    (uint8_t)VoiceId::Piano, (uint8_t)VoiceId::EPiano, (uint8_t)VoiceId::Organ,
    (uint8_t)VoiceId::Synth, (uint8_t)VoiceId::Bass, (uint8_t)VoiceId::Strings,
    (uint8_t)VoiceId::Brass, (uint8_t)VoiceId::Mallets, (uint8_t)VoiceId::Reed,
    (uint8_t)VoiceId::Guitar, (uint8_t)VoiceId::Drums, (uint8_t)VoiceId::None,
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
