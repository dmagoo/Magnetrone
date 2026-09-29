#include "audio.h"
#include <Audio.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "config/storage.h"
#include "kit.h"
#include "fx_chorus.h"
#include "fx_delay.h"

// ----------------------------------------------------------------------------
// Signal chain:
//   Per voice:  AudioSynthWaveform -> AudioEffectEnvelope -> voice filter
//               (a state variable low-pass, which its own envelope opens and
//               closes: AudioSynthWaveformDc -> AudioEffectEnvelope -> its
//               frequency input)
//   Per layer:  8 voices -> four AudioMixer4 sub-mixers: two take the voices
//               before their filters, two after, so the filter can be left out
//               -> melIn -> layerIn, which also takes the drums when the layer
//               plays them
//               -> Tone -> Chorus -> Delay -> Reverb -> mixOut
//   Drums:      one shared kit, 9 sources -> three drumMix sub-mixers -> drumSum
//   Output:     mixOut -> AudioOutputI2S -> SGTL5000
//
// Each effect has a mixer after it that blends its output with its input (or,
// for Tone, picks one), so an effect at Off passes its input straight on. The
// effects are the layer's, applied to its whole mix, not per voice.
// Tone and Chorus only colour the sound; Delay and Reverb have memory, and
// keep running while their Mix is Off.
//
// One bank of NUM_HALL_SENSORS voices per layer, so Layer A and Layer B can
// play different voices at once. Layer level is not applied here: it rides on
// note velocity (see sequencer.cpp), so external synths hear it too.
//
// The drum kit is a single bank shared by whichever layers play a kit voice.
// It goes through one layer's effects: the layer playing it, or Layer A's
// when both do (audioSetDrumLayer()).
// Each drum has its own source and its own mixer input, so velocity is applied
// as that input's gain at each hit (AudioSynthSimpleDrum has no amplitude).
// ----------------------------------------------------------------------------

constexpr uint8_t VOICES_PER_LAYER = NUM_HALL_SENSORS;
constexpr uint8_t TOTAL_VOICES     = VOICES_PER_LAYER * NUM_LAYERS;

static AudioSynthWaveform       osc[TOTAL_VOICES];
static AudioEffectEnvelope      env[TOTAL_VOICES];
static AudioFilterStateVariable vfilt[TOTAL_VOICES];
static AudioSynthWaveformDc     fdc[TOTAL_VOICES];    // how far the filter envelope opens
static AudioEffectEnvelope      fenv[TOTAL_VOICES];

// Four per layer: sub[4 * l + 0], [+ 1] = voices 0-3, 4-7 without the filter;
// [+ 2], [+ 3] the same voices through it.
static AudioMixer4            sub[4 * NUM_LAYERS];
static AudioMixer4            melIn[NUM_LAYERS];

// Drum kit. Pitched drums use AudioSynthSimpleDrum; the rest share one white
// noise source, each through its own filter and envelope. Both hats share a
// filter, since they differ only in decay.
static AudioSynthSimpleDrum     kick, lowTom, highTom, snareBody;
static AudioSynthNoiseWhite     noise;
static AudioFilterStateVariable snareFilt, clapFilt, hatFilt, crashFilt;
static AudioEffectEnvelope      snareEnv, clapEnv, closedHatEnv, openHatEnv, crashEnv;
static AudioMixer4              drumMix[3];
static AudioMixer4              drumSum;

// Each layer's effects chain. layerIn: 0 = the layer's voices, 2 = the
// drums. Each *Out mixer: 0 = the effect's input (dry), 1 = its output.
static AudioMixer4            layerIn[NUM_LAYERS];
static AudioFilterLadder      toneFilt[NUM_LAYERS];
static AudioMixer4            toneOut[NUM_LAYERS];
static AudioEffectModChorus   chorus[NUM_LAYERS];
static AudioMixer4            chorusOut[NUM_LAYERS];
static AudioEffectFbDelay     delayFx[NUM_LAYERS];
static AudioMixer4            delayOut[NUM_LAYERS];
static AudioEffectFreeverb    reverb[NUM_LAYERS];
static AudioMixer4            reverbOut[NUM_LAYERS];

static AudioMixer4            mixOut;

static AudioOutputI2S         i2sOut;
static AudioControlSGTL5000   sgtl5000;

// Patch cords -- must be statically allocated. Voice v of layer v / 8 sits
// at input v % 4 of that layer's sub-mixers for its half. State variable
// filter outputs: 0 = low pass.
#define SUB_DRY(v)  sub[4 * ((v) / 8) + ((v) % 8) / 4]
#define SUB_FILT(v) sub[4 * ((v) / 8) + ((v) % 8) / 4 + 2]
#define OSC_ENV(v) static AudioConnection patchOscEnv##v (osc[v],   0, env[v],      0);        \
                   static AudioConnection patchEnvDry##v (env[v],   0, SUB_DRY(v),  (v) % 4);  \
                   static AudioConnection patchEnvFilt##v(env[v],   0, vfilt[v],    0);        \
                   static AudioConnection patchDcFenv##v (fdc[v],   0, fenv[v],     0);        \
                   static AudioConnection patchFenv##v   (fenv[v],  0, vfilt[v],    1);        \
                   static AudioConnection patchFiltSub##v(vfilt[v], 0, SUB_FILT(v), (v) % 4);
OSC_ENV(0)  OSC_ENV(1)  OSC_ENV(2)  OSC_ENV(3)
OSC_ENV(4)  OSC_ENV(5)  OSC_ENV(6)  OSC_ENV(7)
OSC_ENV(8)  OSC_ENV(9)  OSC_ENV(10) OSC_ENV(11)
OSC_ENV(12) OSC_ENV(13) OSC_ENV(14) OSC_ENV(15)
#undef OSC_ENV
#undef SUB_DRY
#undef SUB_FILT

static AudioConnection patchSubA0(sub[0], 0, melIn[LAYER_A], 0);
static AudioConnection patchSubA1(sub[1], 0, melIn[LAYER_A], 1);
static AudioConnection patchSubA2(sub[2], 0, melIn[LAYER_A], 2);
static AudioConnection patchSubA3(sub[3], 0, melIn[LAYER_A], 3);
static AudioConnection patchSubB0(sub[4], 0, melIn[LAYER_B], 0);
static AudioConnection patchSubB1(sub[5], 0, melIn[LAYER_B], 1);
static AudioConnection patchSubB2(sub[6], 0, melIn[LAYER_B], 2);
static AudioConnection patchSubB3(sub[7], 0, melIn[LAYER_B], 3);
static AudioConnection patchMelA (melIn[LAYER_A], 0, layerIn[LAYER_A], 0);
static AudioConnection patchMelB (melIn[LAYER_B], 0, layerIn[LAYER_B], 0);

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

static AudioConnection patchDrumSum0(drumMix[0], 0, drumSum, 0);
static AudioConnection patchDrumSum1(drumMix[1], 0, drumSum, 1);
static AudioConnection patchDrumSum2(drumMix[2], 0, drumSum, 2);
static AudioConnection patchDrumA   (drumSum,    0, layerIn[LAYER_A], 2);
static AudioConnection patchDrumB   (drumSum,    0, layerIn[LAYER_B], 2);

// The delay buffers: 16-bit at the audio rate, DELAY_MAX_MS each, in RAM2
// (DMAMEM). Fixed at build time, so buffers that do not fit fail the link.
constexpr uint32_t DELAY_SAMPLES = (uint32_t)(DELAY_MAX_MS * 44100UL / 1000UL) + 1;
DMAMEM static int16_t delayBuf[NUM_LAYERS][DELAY_SAMPLES];

// The effects chains, one per layer.
#define FX_CHAIN(l) \
    static AudioConnection patchToneIn##l   (layerIn[l],   0, toneFilt[l],  0); \
    static AudioConnection patchToneDry##l  (layerIn[l],   0, toneOut[l],   0); \
    static AudioConnection patchToneWet##l  (toneFilt[l],  0, toneOut[l],   1); \
    static AudioConnection patchChorusIn##l (toneOut[l],   0, chorus[l],    0); \
    static AudioConnection patchChorusDry##l(toneOut[l],   0, chorusOut[l], 0); \
    static AudioConnection patchChorusWet##l(chorus[l],    0, chorusOut[l], 1); \
    static AudioConnection patchDelayIn##l  (chorusOut[l], 0, delayFx[l],   0); \
    static AudioConnection patchDelayDry##l (chorusOut[l], 0, delayOut[l],  0); \
    static AudioConnection patchDelayWet##l (delayFx[l],   0, delayOut[l],  1); \
    static AudioConnection patchReverbIn##l (delayOut[l],  0, reverb[l],    0); \
    static AudioConnection patchReverbDry##l(delayOut[l],  0, reverbOut[l], 0); \
    static AudioConnection patchReverbWet##l(reverb[l],    0, reverbOut[l], 1); \
    static AudioConnection patchLayerOut##l (reverbOut[l], 0, mixOut,       l);
FX_CHAIN(0) FX_CHAIN(1)
#undef FX_CHAIN

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

// Output staging. Both layers at full would clip once they sound together,
// so each takes half, as the melodic and drum buses did before the effects.
constexpr float LAYER_OUT_GAIN = 0.5f;

// Tone Cutoff, percent to Hz: 0 to 90% sweeps exponentially between these;
// FX_CUTOFF_OFF bypasses the filter. Resonance stops short of the ladder
// filter's self-oscillation at 1.0.
constexpr float CUTOFF_MIN_HZ = 60.0f;
constexpr float CUTOFF_MAX_HZ = 16000.0f;
constexpr float CUTOFF_TOP_PCT = 90.0f;
constexpr float RESONANCE_MAX = 0.9f;

// The voice filter's resonance, percent to the state variable filter's Q
// (0.707 is flat), and how many octaves its envelope opens it at Amount 100%.
// Its cutoff maps as the Tone's does.
constexpr float VOICE_Q_MIN          = 0.707f;
constexpr float VOICE_Q_MAX          = 4.0f;
constexpr float VOICE_FILTER_OCTAVES = 7.0f;

// Chorus Rate, percent to Hz, exponentially; Depth, percent of the full sweep.
constexpr float CHORUS_RATE_MIN_HZ = 0.1f;
constexpr float CHORUS_RATE_MAX_HZ = 5.0f;

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

// The level the volume knob (or MIDI CC) last set, and whether the output is
// muted. Kept here so unmuting can restore the level, and so a volume change
// while muted stays silent instead of quietly unmuting.
static float currentVolume = 0.0f;
static bool  isMuted       = false;

void audioInit(float volume, bool muted) {
    currentVolume = volume;
    isMuted       = muted;
    // 16 voices with their filters, the drum kit, the mixers and the
    // effects. Running short of blocks fails as silent dropouts, not an
    // error, so this is sized with headroom rather than to the minimum.
    AudioMemory(160);

    sgtl5000.enable();
    sgtl5000.volume(muted ? 0.0f : volume);

    // 0.25 per voice * 4 voices = 1.0 max per sub-mixer, as before. Each
    // voice feeds two of them, before and after its filter; audioSetVoice()
    // opens one and closes the other.
    for (int m = 0; m < 4 * NUM_LAYERS; m++) {
        for (int i = 0; i < 4; i++) sub[m].gain(i, 0.25f);
    }
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        for (int i = 0; i < 4; i++) melIn[l].gain(i, 1.0f);
    }
    // Each layer's pair sums as the single bank did before the layers existed.
    // Both banks full double the theoretical peak, but each sensor fires one
    // pole per pass, so the notes sounding at once stay about what the single
    // bank carried.
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        layerIn[l].gain(0, 1.0f);
        layerIn[l].gain(1, 0.0f);
        layerIn[l].gain(3, 0.0f);
        mixOut.gain(l, LAYER_OUT_GAIN);
        delayFx[l].begin(delayBuf[l], DELAY_SAMPLES);
        audioSetEffects(l, storageFactoryFx());
    }
    for (int i = 2; i < 4; i++) mixOut.gain(i, 0.0f);
    for (int i = 0; i < 3; i++) drumSum.gain(i, 1.0f);
    drumSum.gain(3, 0.0f);
    audioSetDrumLayer(LAYER_A);

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
        vfilt[i].octaveControl(VOICE_FILTER_OCTAVES);
        fenv[i].hold(0.0f);
    }
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        audioSetVoice(l, voiceGet((uint8_t)VoiceId::Piano));   // caller sets the saved ones
    }
}

// Percent to Hz, for the Tone and the voice filter alike.
static float cutoffHz(uint8_t pct) {
    float t = min((float)pct, CUTOFF_TOP_PCT) / CUTOFF_TOP_PCT;
    return CUTOFF_MIN_HZ * powf(CUTOFF_MAX_HZ / CUTOFF_MIN_HZ, t);
}

// Harmonic waves. Each layer's 256-point wave is built from its voice's
// harmonic levels, and rebuilt only when they change. Two tables per layer:
// the new wave is built in the one not playing, then the oscillators switch
// to it, so a change mid-note never plays a half-written wave.
constexpr uint16_t WAVE_POINTS = 256;
static int16_t harmWave[NUM_LAYERS][2][WAVE_POINTS];
static uint8_t harmWaveCur[NUM_LAYERS] = {};
static uint8_t harmBuilt[NUM_LAYERS][NUM_HARMONICS];
static bool    harmValid[NUM_LAYERS] = {};

// Harmonic n at point i is SINE[(n * i) % 256], so the whole build needs
// only one period of sine.
static float harmSine[WAVE_POINTS];
static bool  harmSineReady = false;

static const int16_t* harmonicWave(uint8_t layer, const uint8_t levels[NUM_HARMONICS]) {
    if (harmValid[layer] && memcmp(harmBuilt[layer], levels, NUM_HARMONICS) == 0) {
        return harmWave[layer][harmWaveCur[layer]];
    }
    if (!harmSineReady) {
        for (uint16_t i = 0; i < WAVE_POINTS; i++) harmSine[i] = sinf(TWO_PI * i / WAVE_POINTS);
        harmSineReady = true;
    }
    float sum[WAVE_POINTS];
    float peak = 0.0f;
    for (uint16_t i = 0; i < WAVE_POINTS; i++) {
        float s = 0.0f;
        for (uint8_t h = 0; h < NUM_HARMONICS; h++) {
            if (levels[h]) s += levels[h] * harmSine[((h + 1) * i) % WAVE_POINTS];
        }
        sum[i] = s;
        if (fabsf(s) > peak) peak = fabsf(s);
    }
    // Scaled to full range, so only the ratios between the levels matter.
    // All levels at 0 is silence.
    float scale = (peak > 0.0f) ? 32767.0f / peak : 0.0f;
    uint8_t next = harmWaveCur[layer] ^ 1;
    for (uint16_t i = 0; i < WAVE_POINTS; i++) {
        harmWave[layer][next][i] = (int16_t)lroundf(sum[i] * scale);
    }
    harmWaveCur[layer] = next;
    memcpy(harmBuilt[layer], levels, NUM_HARMONICS);
    harmValid[layer] = true;
    return harmWave[layer][next];
}

void audioSetVoice(uint8_t layer, const Voice& voice) {
    if (layer >= NUM_LAYERS) return;
    // A kit voice plays the shared drum bank and a silent one plays nothing;
    // either way this layer's synth bank sits idle, so leave it as it was.
    if (voiceIsKit(voice) || voiceIsSilent(voice)) return;
    const int16_t* wave = nullptr;
    if (voice.harmonicsEdited) wave = harmonicWave(layer, voice.harmonics);
    const VoiceFilter& f = voice.filter;
    for (int i = layer * VOICES_PER_LAYER; i < (layer + 1) * VOICES_PER_LAYER; i++) {
        if (wave) osc[i].arbitraryWaveform(wave, 0.0f);
        osc[i].begin(wave ? WAVEFORM_ARBITRARY : voice.waveform);
        env[i].attack(voice.attackMs);
        env[i].decay(voice.decayMs);
        env[i].sustain(voice.sustain);
        env[i].release(voice.releaseMs);
        vfilt[i].frequency(cutoffHz(f.cutoff));
        vfilt[i].resonance(VOICE_Q_MIN + constrain(f.resonance, 0, 100) / 100.0f * (VOICE_Q_MAX - VOICE_Q_MIN));
        fdc[i].amplitude(constrain(f.amount, 0, 100) / 100.0f);
        fenv[i].attack(f.attackMs);
        fenv[i].decay(f.decayMs);
        fenv[i].sustain(constrain(f.sustainPct, 0, 100) / 100.0f);
        fenv[i].release(f.releaseMs);
    }
    // Cutoff Off leaves the filter out: the voices reach the layer unfiltered.
    bool on = f.cutoff < VOICE_FILTER_OFF;
    for (int h = 0; h < 2; h++) {
        for (int i = 0; i < 4; i++) {
            sub[4 * layer + h].gain(i,     on ? 0.0f : 0.25f);
            sub[4 * layer + h + 2].gain(i, on ? 0.25f : 0.0f);
        }
    }
}

// Mix, percent: an equal-sum crossfade from dry (0) to wet (100).
static void setMix(AudioMixer4& m, uint8_t pct) {
    float wet = constrain(pct, 0, 100) / 100.0f;
    m.gain(0, 1.0f - wet);
    m.gain(1, wet);
    m.gain(2, 0.0f);
    m.gain(3, 0.0f);
}

void audioSetEffects(uint8_t layer, const LayerFx& fx) {
    if (layer >= NUM_LAYERS) return;
    uint8_t l = layer;

    // Tone. At Off the mixer takes the dry input only, so the filter is out
    // of the sound entirely and Resonance does nothing.
    if (fx.cutoff >= FX_CUTOFF_OFF) {
        toneOut[l].gain(0, 1.0f);
        toneOut[l].gain(1, 0.0f);
    } else {
        toneFilt[l].frequency(cutoffHz(fx.cutoff));
        toneFilt[l].resonance(constrain(fx.resonance, 0, 100) / 100.0f * RESONANCE_MAX);
        toneOut[l].gain(0, 0.0f);
        toneOut[l].gain(1, 1.0f);
    }
    toneOut[l].gain(2, 0.0f);
    toneOut[l].gain(3, 0.0f);

    // Chorus.
    float r = constrain(fx.chorusRate, 0, 100) / 100.0f;
    chorus[l].rate(CHORUS_RATE_MIN_HZ * powf(CHORUS_RATE_MAX_HZ / CHORUS_RATE_MIN_HZ, r));
    chorus[l].depth(constrain(fx.chorusDepth, 0, 100) / 100.0f * AudioEffectModChorus::CHORUS_MAX_DEPTH_MS);
    setMix(chorusOut[l], fx.chorusMix);

    // Delay. The time is set apart (audioSetDelayTime()). Like the Reverb it
    // keeps running at Mix 0.
    delayFx[l].feedback(min(fx.delayFeedback, FX_FEEDBACK_MAX) / 100.0f);
    setMix(delayOut[l], fx.delayMix);

    // Reverb. It keeps running at Mix 0, so a tail fades out naturally when
    // the mix comes down and nothing stale plays when it goes back up.
    reverb[l].roomsize(constrain(fx.roomSize, 0, 100) / 100.0f);
    reverb[l].damping(constrain(fx.damping, 0, 100) / 100.0f);
    setMix(reverbOut[l], fx.reverbMix);
}

void audioSetDelayTime(uint8_t layer, float ms) {
    if (layer >= NUM_LAYERS) return;
    delayFx[layer].delayMs(ms);
}

void audioSetDrumLayer(uint8_t layer) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) layerIn[l].gain(2, (l == layer) ? 1.0f : 0.0f);
}

void audioSetVolume(float volume) {
    currentVolume = volume;
    if (!isMuted) sgtl5000.volume(volume);
}

void audioMute() {
    isMuted = true;
    sgtl5000.volume(0.0f);
}

void audioUnmute() {
    isMuted = false;
    sgtl5000.volume(currentVolume);
}

void audioNoteOnFreq(uint8_t layer, uint8_t note, uint8_t velocity, float hz) {
    if (layer >= NUM_LAYERS) return;
    uint8_t v = allocVoice(layer);

    // If this voice was already playing, release it cleanly first.
    env[v].noteOff();
    fenv[v].noteOff();

    float amp = (velocity / 127.0f);
    osc[v].frequency(hz);
    osc[v].amplitude(amp);
    env[v].noteOn();
    fenv[v].noteOn();

    voiceNote[v] = (int8_t)note;
}

void audioNoteOff(uint8_t layer, uint8_t note) {
    if (layer >= NUM_LAYERS) return;
    // Release all voices in this bank playing this note (usually just one).
    for (int i = layer * VOICES_PER_LAYER; i < (layer + 1) * VOICES_PER_LAYER; i++) {
        if (voiceNote[i] == (int8_t)note) {
            env[i].noteOff();
            fenv[i].noteOff();
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
