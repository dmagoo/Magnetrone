#include "audio.h"
#include <Audio.h>
#include <Wire.h>
#include "config.h"
#include "config/storage.h"

// ----------------------------------------------------------------------------
// Signal chain:
//   Per voice:  AudioSynthWaveform -> AudioEffectEnvelope
//   Per layer:  8 voices -> two AudioMixer4 sub-mixers (4 voices each)
//   Output:     all four sub-mixers -> mixOut -> AudioOutputI2S -> SGTL5000
//
// One bank of NUM_HALL_SENSORS voices per layer, so Layer A and Layer B can
// play different voices at once. Layer level is not applied here: it rides on
// note velocity (see sequencer.cpp), so external synths hear it too.
// ----------------------------------------------------------------------------

constexpr uint8_t VOICES_PER_LAYER = NUM_HALL_SENSORS;
constexpr uint8_t TOTAL_VOICES     = VOICES_PER_LAYER * NUM_LAYERS;

static AudioSynthWaveform     osc[TOTAL_VOICES];
static AudioEffectEnvelope    env[TOTAL_VOICES];

// sub[0], sub[1] = Layer A voices 0-3, 4-7; sub[2], sub[3] = Layer B.
static AudioMixer4            sub[4];
static AudioMixer4            mixOut;

static AudioOutputI2S         i2sOut;
static AudioControlSGTL5000   sgtl5000;

// Patch cords -- must be statically allocated. Voice v lives in sub[v / 4],
// input v % 4.
#define OSC_ENV(v) static AudioConnection patchOscEnv##v(osc[v], 0, env[v], 0); \
                   static AudioConnection patchEnvSub##v(env[v], 0, sub[(v) / 4], (v) % 4);
OSC_ENV(0)  OSC_ENV(1)  OSC_ENV(2)  OSC_ENV(3)
OSC_ENV(4)  OSC_ENV(5)  OSC_ENV(6)  OSC_ENV(7)
OSC_ENV(8)  OSC_ENV(9)  OSC_ENV(10) OSC_ENV(11)
OSC_ENV(12) OSC_ENV(13) OSC_ENV(14) OSC_ENV(15)
#undef OSC_ENV

static AudioConnection patchSubOut0(sub[0], 0, mixOut, 0);
static AudioConnection patchSubOut1(sub[1], 0, mixOut, 1);
static AudioConnection patchSubOut2(sub[2], 0, mixOut, 2);
static AudioConnection patchSubOut3(sub[3], 0, mixOut, 3);

// Stereo output -- same mono mix on both channels.
static AudioConnection patchOutL(mixOut, 0, i2sOut, 0);
static AudioConnection patchOutR(mixOut, 0, i2sOut, 1);

// ----------------------------------------------------------------------------

// Tracks which note id each voice is playing so audioNoteOff() knows which
// envelope to release.
static int8_t voiceNote[TOTAL_VOICES];  // -1 = idle

// Round-robin allocator within each layer's bank.
static uint8_t nextVoice[NUM_LAYERS] = {};
static uint8_t allocVoice(uint8_t layer) {
    uint8_t v = nextVoice[layer];
    nextVoice[layer] = (v + 1) % VOICES_PER_LAYER;
    return layer * VOICES_PER_LAYER + v;
}

// ----------------------------------------------------------------------------

void audioInit(float volume, bool muted) {
    // 16 voices plus mixers. Running short of blocks fails as silent dropouts,
    // not an error, so this is sized with headroom rather than to the minimum.
    AudioMemory(48);

    sgtl5000.enable();
    sgtl5000.volume(muted ? 0.0f : volume);

    // 0.25 per voice * 4 voices = 1.0 max per sub-mixer, as before.
    for (int m = 0; m < 4; m++) {
        for (int i = 0; i < 4; i++) sub[m].gain(i, 0.25f);
    }
    // Each layer's pair sums as the single bank did before the layers existed,
    // so Layer A alone is exactly as loud as it used to be. Both banks full
    // double the theoretical peak, but each sensor fires one pole per pass, so
    // the notes sounding at once stay about what the single bank carried.
    for (int i = 0; i < 4; i++) mixOut.gain(i, 1.0f);

    for (int i = 0; i < TOTAL_VOICES; i++) {
        osc[i].begin(1.0f, 440.0f, WAVEFORM_SINE);
        osc[i].amplitude(0.0f); // silent until a note fires
        voiceNote[i] = -1;
    }
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        audioSetVoice(l, voiceGet((uint8_t)VoiceId::Piano));   // caller sets the saved ones
    }
}

void audioSetVoice(uint8_t layer, const Voice& voice) {
    if (layer >= NUM_LAYERS) return;
    for (int i = layer * VOICES_PER_LAYER; i < (layer + 1) * VOICES_PER_LAYER; i++) {
        osc[i].begin(voice.waveform);
        env[i].attack(voice.attackMs);
        env[i].decay(voice.decayMs);
        env[i].sustain(voice.sustain);
        env[i].release(voice.releaseMs);
    }
}

void audioSetVolume(float volume) {
    sgtl5000.volume(volume);
}

void audioMute() {
    sgtl5000.volume(0.0f);
}

void audioUnmute() {
    // Volume is managed by the SGTL5000 -- re-read from the caller's cfg.
    // audioSetVolume() is called by the menu after unmuting.
}

void audioNoteOnFreq(uint8_t layer, uint8_t note, uint8_t velocity, float hz) {
    if (layer >= NUM_LAYERS) return;
    uint8_t v = allocVoice(layer);

    // If this voice was already playing, release it cleanly first.
    env[v].noteOff();

    float amp = (velocity / 127.0f);
    osc[v].frequency(hz);
    osc[v].amplitude(amp);
    env[v].noteOn();

    voiceNote[v] = (int8_t)note;
}

void audioNoteOff(uint8_t layer, uint8_t note) {
    if (layer >= NUM_LAYERS) return;
    // Release all voices in this bank playing this note (usually just one).
    for (int i = layer * VOICES_PER_LAYER; i < (layer + 1) * VOICES_PER_LAYER; i++) {
        if (voiceNote[i] == (int8_t)note) {
            env[i].noteOff();
            voiceNote[i] = -1;
        }
    }
}
