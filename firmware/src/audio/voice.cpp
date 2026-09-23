#include "voice.h"
#include <Audio.h>

// Starting values, agreed 2026-09-23 and expected to be tuned by ear. The
// bandlimited saw and square alias far less than the plain ones, which matters
// on the high octaves. Auto channels follow the scheme in todo.md; Drums will
// take 10, per General MIDI.
static const Voice VOICES[VOICE_COUNT] = {
    //  name       waveform                      A    D    S      R    note  ch
    { "Piano",   WAVEFORM_TRIANGLE,             5, 400, 0.2f, 400, 250,  1 },
    { "Strings", WAVEFORM_BANDLIMIT_SAWTOOTH, 150, 100, 0.8f, 600, 800,  3 },
    { "Leads",   WAVEFORM_BANDLIMIT_SQUARE,     5, 100, 0.7f, 150, 200,  4 },
    { "Bass",    WAVEFORM_BANDLIMIT_SAWTOOTH,   5, 150, 0.5f, 100, 250,  2 },
};

const Voice& voiceGet(uint8_t id) {
    return VOICES[(id < VOICE_COUNT) ? id : (uint8_t)VoiceId::Piano];
}
