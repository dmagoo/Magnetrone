#include "audio.h"
#include <Audio.h>
#include <Wire.h>
#include "config.h"

// ----------------------------------------------------------------------------
// Signal chain (per voice):
//   AudioSynthWaveform -> AudioEffectEnvelope -> AudioMixer4 (two banks of 4)
//   Both mixers feed a final AudioMixer4 -> AudioOutputI2S -> SGTL5000
//
// NUM_HALL_SENSORS is 8, so we need two 4-channel mixers to sum all voices.
// ----------------------------------------------------------------------------

static AudioSynthWaveform     osc[NUM_HALL_SENSORS];
static AudioEffectEnvelope    env[NUM_HALL_SENSORS];

// Two sub-mixers, each taking 4 voices.
static AudioMixer4            mixA;   // voices 0-3
static AudioMixer4            mixB;   // voices 4-7
static AudioMixer4            mixOut; // sums mixA + mixB -> output

static AudioOutputI2S         i2sOut;
static AudioControlSGTL5000   sgtl5000;

// Patch cords -- must be statically allocated.
static AudioConnection patchOscEnv0(osc[0], 0, env[0], 0);
static AudioConnection patchOscEnv1(osc[1], 0, env[1], 0);
static AudioConnection patchOscEnv2(osc[2], 0, env[2], 0);
static AudioConnection patchOscEnv3(osc[3], 0, env[3], 0);
static AudioConnection patchOscEnv4(osc[4], 0, env[4], 0);
static AudioConnection patchOscEnv5(osc[5], 0, env[5], 0);
static AudioConnection patchOscEnv6(osc[6], 0, env[6], 0);
static AudioConnection patchOscEnv7(osc[7], 0, env[7], 0);

static AudioConnection patchEnvMixA0(env[0], 0, mixA, 0);
static AudioConnection patchEnvMixA1(env[1], 0, mixA, 1);
static AudioConnection patchEnvMixA2(env[2], 0, mixA, 2);
static AudioConnection patchEnvMixA3(env[3], 0, mixA, 3);

static AudioConnection patchEnvMixB0(env[4], 0, mixB, 0);
static AudioConnection patchEnvMixB1(env[5], 0, mixB, 1);
static AudioConnection patchEnvMixB2(env[6], 0, mixB, 2);
static AudioConnection patchEnvMixB3(env[7], 0, mixB, 3);

// Sub-mixers into the output mixer. Channels 2 and 3 of mixOut are unused.
static AudioConnection patchMixAOut(mixA, 0, mixOut, 0);
static AudioConnection patchMixBOut(mixB, 0, mixOut, 1);

// Stereo output -- same mono mix on both channels.
static AudioConnection patchOutL(mixOut, 0, i2sOut, 0);
static AudioConnection patchOutR(mixOut, 0, i2sOut, 1);

// ----------------------------------------------------------------------------

// Converts a MIDI note number to frequency in Hz.
// A4 = 69 = 440 Hz.
static float midiToHz(uint8_t note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

// Tracks which voice slot is playing each MIDI note so audioNoteOff()
// knows which envelope to release.
static int8_t voiceNote[NUM_HALL_SENSORS];  // -1 = idle

// Round-robin voice allocator -- returns 0..NUM_HALL_SENSORS-1.
static uint8_t nextVoice = 0;
static uint8_t allocVoice() {
    uint8_t v = nextVoice;
    nextVoice = (nextVoice + 1) % NUM_HALL_SENSORS;
    return v;
}

// ----------------------------------------------------------------------------

void audioInit(float volume, bool muted) {
    AudioMemory(24);

    sgtl5000.enable();
    sgtl5000.volume(muted ? 0.0f : volume);

    // Set gain on sub-mixers so 8 voices at full amplitude don't clip.
    // 0.25 per voice * 4 voices per mixer = 1.0 max per sub-mixer.
    for (int i = 0; i < 4; i++) {
        mixA.gain(i, 0.25f);
        mixB.gain(i, 0.25f);
    }
    // Output mixer: both sub-mixers at full gain (already scaled above).
    mixOut.gain(0, 1.0f);
    mixOut.gain(1, 1.0f);

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        osc[i].begin(1.0f, 440.0f, WAVEFORM_SINE);
        osc[i].amplitude(0.0f); // silent until a note fires
        voiceNote[i] = -1;
    }
    audioSetVoice(voiceGet((uint8_t)VoiceId::Piano));   // caller sets the saved one
}

void audioSetVoice(const Voice& voice) {
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
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

void audioNoteOn(uint8_t note, uint8_t velocity) {
    audioNoteOnFreq(note, velocity, midiToHz(note));
}

void audioNoteOnFreq(uint8_t note, uint8_t velocity, float hz) {
    uint8_t v = allocVoice();

    // If this voice was already playing, release it cleanly first.
    env[v].noteOff();

    float amp = (velocity / 127.0f);
    osc[v].frequency(hz);
    osc[v].amplitude(amp);
    env[v].noteOn();

    voiceNote[v] = (int8_t)note;
}

void audioNoteOff(uint8_t note) {
    // Release all voices playing this note (usually just one).
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        if (voiceNote[i] == (int8_t)note) {
            env[i].noteOff();
            voiceNote[i] = -1;
        }
    }
}
