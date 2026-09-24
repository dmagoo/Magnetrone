#include "voice.h"
#include <Audio.h>

// Starting values, agreed 2026-09-23 and expected to be tuned by ear. The
// bandlimited saw and square alias far less than the plain ones, which matters
// on the high octaves. Auto channels: Piano 1, Bass 2, Strings 3, Leads 4; Drums
// take 10, per General MIDI. Drums ignore waveform and envelope (the drum bank has
// its own per-drum sounds); its note length only times the MIDI Note Off.
static const Voice VOICES[VOICE_COUNT] = {
    //  name       waveform                      A    D    S      R    note  ch  source
    { "Piano",   WAVEFORM_TRIANGLE,             5, 400, 0.2f, 400, 250,  1, NoteSource::Scale },
    { "Strings", WAVEFORM_BANDLIMIT_SAWTOOTH, 150, 100, 0.8f, 600, 800,  3, NoteSource::Scale },
    { "Leads",   WAVEFORM_BANDLIMIT_SQUARE,     5, 100, 0.7f, 150, 200,  4, NoteSource::Scale },
    { "Bass",    WAVEFORM_BANDLIMIT_SAWTOOTH,   5, 150, 0.5f, 100, 250,  2, NoteSource::Scale },
    { "Drums",   WAVEFORM_SINE,                 0,   0, 0.0f,   0,  50, 10, NoteSource::Kit   },
};

const Voice& voiceGet(uint8_t id) {
    return VOICES[(id < VOICE_COUNT) ? id : (uint8_t)VoiceId::Piano];
}
