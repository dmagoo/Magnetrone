#include "audio.h"
#include <Audio.h>
#include <Wire.h>
#include "config.h"
#include "config/storage.h"
#include "kit.h"

// ----------------------------------------------------------------------------
// Signal chain:
//   Per voice:  AudioSynthWaveform -> AudioEffectEnvelope
//   Per layer:  8 voices -> two AudioMixer4 sub-mixers (4 voices each)
//   Melodic:    all four sub-mixers -> melMix
//   Drums:      one shared kit, 9 sources -> three drumMix sub-mixers
//   Output:     melMix + drumMix[0..2] -> mixOut -> AudioOutputI2S -> SGTL5000
//
// One bank of NUM_HALL_SENSORS voices per layer, so Layer A and Layer B can
// play different voices at once. Layer level is not applied here: it rides on
// note velocity (see sequencer.cpp), so external synths hear it too.
//
// The drum kit is a single bank shared by whichever layers play a kit voice.
// Each drum has its own source and its own mixer input, so velocity is applied
// as that input's gain at each hit (AudioSynthSimpleDrum has no amplitude).
// ----------------------------------------------------------------------------

constexpr uint8_t VOICES_PER_LAYER = NUM_HALL_SENSORS;
constexpr uint8_t TOTAL_VOICES     = VOICES_PER_LAYER * NUM_LAYERS;

static AudioSynthWaveform     osc[TOTAL_VOICES];
static AudioEffectEnvelope    env[TOTAL_VOICES];

// sub[0], sub[1] = Layer A voices 0-3, 4-7; sub[2], sub[3] = Layer B.
static AudioMixer4            sub[4];
static AudioMixer4            melMix;

// Drum kit. Pitched drums use AudioSynthSimpleDrum; the rest share one white
// noise source, each through its own filter and envelope. Both hats share a
// filter, since they differ only in decay.
static AudioSynthSimpleDrum     kick, lowTom, highTom, snareBody;
static AudioSynthNoiseWhite     noise;
static AudioFilterStateVariable snareFilt, clapFilt, hatFilt, crashFilt;
static AudioEffectEnvelope      snareEnv, clapEnv, closedHatEnv, openHatEnv, crashEnv;
static AudioMixer4              drumMix[3];

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

static AudioConnection patchSubMel0(sub[0], 0, melMix, 0);
static AudioConnection patchSubMel1(sub[1], 0, melMix, 1);
static AudioConnection patchSubMel2(sub[2], 0, melMix, 2);
static AudioConnection patchSubMel3(sub[3], 0, melMix, 3);

// Noise fans out to every noise drum's filter. State variable filter outputs:
// 0 = low pass, 1 = band pass, 2 = high pass.
static AudioConnection patchNoiseSnare(noise, 0, snareFilt, 0);
static AudioConnection patchNoiseClap (noise, 0, clapFilt,  0);
static AudioConnection patchNoiseHat  (noise, 0, hatFilt,   0);
static AudioConnection patchNoiseCrash(noise, 0, crashFilt, 0);
static AudioConnection patchSnareEnv  (snareFilt, 2, snareEnv,     0);
static AudioConnection patchClapEnv   (clapFilt,  1, clapEnv,      0);
static AudioConnection patchClosedHat (hatFilt,   2, closedHatEnv, 0);
static AudioConnection patchOpenHat   (hatFilt,   2, openHatEnv,   0);
static AudioConnection patchCrashEnv  (crashFilt, 2, crashEnv,     0);

// Drum mixer inputs. DRUM_INPUT[] below must match.
static AudioConnection patchKick     (kick,         0, drumMix[0], 0);
static AudioConnection patchLowTom   (lowTom,       0, drumMix[0], 1);
static AudioConnection patchHighTom  (highTom,      0, drumMix[0], 2);
static AudioConnection patchSnareBody(snareBody,    0, drumMix[0], 3);
static AudioConnection patchSnareMix (snareEnv,     0, drumMix[1], 0);
static AudioConnection patchClapMix  (clapEnv,      0, drumMix[1], 1);
static AudioConnection patchClosedMix(closedHatEnv, 0, drumMix[1], 2);
static AudioConnection patchOpenMix  (openHatEnv,   0, drumMix[1], 3);
static AudioConnection patchCrashMix (crashEnv,     0, drumMix[2], 0);

static AudioConnection patchMelOut  (melMix,     0, mixOut, 0);
static AudioConnection patchDrumOut0(drumMix[0], 0, mixOut, 1);
static AudioConnection patchDrumOut1(drumMix[1], 0, mixOut, 2);
static AudioConnection patchDrumOut2(drumMix[2], 0, mixOut, 3);

// Stereo output -- same mono mix on both channels.
static AudioConnection patchOutL(mixOut, 0, i2sOut, 0);
static AudioConnection patchOutR(mixOut, 0, i2sOut, 1);

// ----------------------------------------------------------------------------

// Where each slot's drum enters the drum mixers, and its level relative to the
// rest of the kit at full velocity. The snare has a second input for its noise
// (mixer 1, input 0). Starting values, to be tuned by ear.
struct DrumInput { uint8_t mixer; uint8_t input; float trim; };
static const DrumInput DRUM_INPUT[NUM_HALL_SENSORS] = {
    { 2, 0, 0.40f },   // Crash
    { 0, 1, 0.80f },   // LowTom
    { 0, 2, 0.80f },   // HighTom
    { 1, 1, 0.70f },   // Clap
    { 1, 3, 0.35f },   // OpenHat
    { 0, 3, 0.60f },   // Snare (body; the noise is mixer 1 input 0)
    { 0, 0, 1.00f },   // Kick
    { 1, 2, 0.35f },   // ClosedHat
};
constexpr float SNARE_NOISE_TRIM = 0.60f;

// Output staging. Both buses at full would clip once both layers sound
// together, so each takes half. Layer A alone is 6 dB quieter than before the
// drums existed; the volume knob has the headroom to cover it.
constexpr float MEL_BUS_GAIN  = 0.5f;
constexpr float DRUM_BUS_GAIN = 0.5f;

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
    // 16 voices, the drum kit and the mixers. Running short of blocks fails as
    // silent dropouts, not an error, so this is sized with headroom rather than
    // to the minimum.
    AudioMemory(64);

    sgtl5000.enable();
    sgtl5000.volume(muted ? 0.0f : volume);

    // 0.25 per voice * 4 voices = 1.0 max per sub-mixer, as before.
    for (int m = 0; m < 4; m++) {
        for (int i = 0; i < 4; i++) sub[m].gain(i, 0.25f);
    }
    // Each layer's pair sums as the single bank did before the layers existed.
    // Both banks full double the theoretical peak, but each sensor fires one
    // pole per pass, so the notes sounding at once stay about what the single
    // bank carried.
    for (int i = 0; i < 4; i++) melMix.gain(i, 1.0f);

    mixOut.gain(0, MEL_BUS_GAIN);
    for (int i = 1; i < 4; i++) mixOut.gain(i, DRUM_BUS_GAIN);

    // Drum inputs stay silent until a hit sets their gain.
    for (int m = 0; m < 3; m++) {
        for (int i = 0; i < 4; i++) drumMix[m].gain(i, 0.0f);
    }

    // Pitched drums. pitchMod 0.5 is a flat pitch; above it sweeps down.
    // Starting values, to be tuned by ear.
    kick.frequency(55);       kick.length(350);      kick.secondMix(0.0f);      kick.pitchMod(0.60f);
    lowTom.frequency(100);    lowTom.length(400);    lowTom.secondMix(0.0f);    lowTom.pitchMod(0.55f);
    highTom.frequency(160);   highTom.length(300);   highTom.secondMix(0.0f);   highTom.pitchMod(0.55f);
    snareBody.frequency(190); snareBody.length(120); snareBody.secondMix(0.3f); snareBody.pitchMod(0.55f);

    // Noise drums: the filter sets the colour, the envelope the length.
    // Sustain 0 makes each a one-shot that decays to silence with no Note Off.
    noise.amplitude(1.0f);
    snareFilt.frequency(1800); snareFilt.resonance(0.7f);
    clapFilt.frequency(1200);  clapFilt.resonance(1.5f);
    hatFilt.frequency(7000);   hatFilt.resonance(0.7f);
    crashFilt.frequency(4500); crashFilt.resonance(0.7f);

    AudioEffectEnvelope* noiseEnv[] = { &snareEnv, &clapEnv, &closedHatEnv, &openHatEnv, &crashEnv };
    for (AudioEffectEnvelope* e : noiseEnv) {
        e->attack(0.5f);
        e->hold(0.0f);
        e->sustain(0.0f);
        e->release(20.0f);      // only reached by the hi-hat choke
        e->releaseNoteOn(1.0f);
    }
    snareEnv.decay(180);
    clapEnv.decay(220);
    closedHatEnv.decay(45);
    openHatEnv.decay(400);
    crashEnv.decay(1500);

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
    // A kit voice plays the shared drum bank; this layer's synth bank sits
    // idle, so leave it as it was.
    if (voiceIsKit(voice)) return;
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

void audioDrumHit(uint8_t slot, uint8_t velocity) {
    if (slot >= NUM_HALL_SENSORS) return;
    float amp = velocity / 127.0f;
    const DrumInput& in = DRUM_INPUT[slot];
    drumMix[in.mixer].gain(in.input, amp * in.trim);

    switch ((DrumSlot)slot) {
        case DrumSlot::Kick:    kick.noteOn();    break;
        case DrumSlot::LowTom:  lowTom.noteOn();  break;
        case DrumSlot::HighTom: highTom.noteOn(); break;
        case DrumSlot::Snare:
            drumMix[1].gain(0, amp * SNARE_NOISE_TRIM);
            snareBody.noteOn();
            snareEnv.noteOn();
            break;
        case DrumSlot::Clap:    clapEnv.noteOn(); break;
        case DrumSlot::ClosedHat:
            // Choke: a closed hat cuts off a ringing open hat, as on a real kit.
            openHatEnv.noteOff();
            closedHatEnv.noteOn();
            break;
        case DrumSlot::OpenHat: openHatEnv.noteOn(); break;
        case DrumSlot::Crash:   crashEnv.noteOn();   break;
        default: break;
    }
}
