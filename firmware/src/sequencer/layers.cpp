#include "layers.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "audio/audio.h"
#include "midi/midi.h"

static int8_t balance = 0;

// Each layer's live copy of its voice, and which voice it is a copy of.
// A layer set to a different voice starts again from stock.
static const uint8_t NO_VOICE = 0xFF;
static Voice   live[NUM_LAYERS];
static uint8_t liveId[NUM_LAYERS]  = { NO_VOICE, NO_VOICE };
static uint8_t liveBase[NUM_LAYERS];   // the built-in it came from, for saving
static bool    tweaked[NUM_LAYERS] = { false, false };

static const char* CUSTOM_NAMES[NUM_CUSTOM_VOICES] = {
    "Custom 1","Custom 2","Custom 3","Custom 4","Custom 5","Custom 6","Custom 7","Custom 8"
};
static const char* SCENE_VOICE_NAME = "Scene Voice";

const char* voiceIdName(uint8_t id) {
    if (voiceIsCustomId(id)) return CUSTOM_NAMES[id - VOICE_CUSTOM_FIRST];
    if (id == VOICE_SCENE)   return SCENE_VOICE_NAME;
    return voiceGet(id).name;
}

// The saved voice a stored id refers to, or null for a built-in (or a slot
// that is empty, which then plays Piano).
static const VoiceSlot* slotFor(const SavedConfig& cfg, uint8_t l, uint8_t id) {
    const VoiceSlot* s = nullptr;
    if (voiceIsCustomId(id))  s = &cfg.customVoices[id - VOICE_CUSTOM_FIRST];
    else if (id == VOICE_SCENE && cfg.currentScene < NUM_SCENES) s = &cfg.sceneVoices[cfg.currentScene][l];
    return (s && s->used) ? s : nullptr;
}

// Whose voice this layer plays: Layer A's for B in Same as A.
static uint8_t voiceSource(const SavedConfig& cfg, uint8_t layer) {
    return (layer == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) ? LAYER_A : layer;
}

static void sync(const SavedConfig& cfg, uint8_t l) {
    uint8_t id = cfg.layer[l].voice;
    if (liveId[l] == id) return;
    const VoiceSlot* s = slotFor(cfg, l, id);
    if (s) {
        live[l]           = voiceGet(s->base);
        live[l].name      = voiceIdName(id);
        uint8_t wave      = s->wave & ~SLOT_HARMONICS_EDITED;
        live[l].waveform  = voiceWaveform((Wave)wave);   // out of range reads as Sine
        live[l].attackMs  = s->attackMs;
        live[l].decayMs   = s->decayMs;
        live[l].sustain   = (float)s->sustainPct / 100.0f;
        live[l].releaseMs = s->releaseMs;
        live[l].noteMs    = s->noteMs;
        memcpy(live[l].harmonics, s->harmonics, NUM_HARMONICS);
        live[l].harmonicsEdited = (s->wave & SLOT_HARMONICS_EDITED) || wave >= WAVE_COUNT;
        live[l].filter    = s->filter;
        liveBase[l]       = s->base;
    } else {
        uint8_t base = (id < VOICE_COUNT) ? id : (uint8_t)VoiceId::Piano;
        live[l]     = voiceGet(base);
        liveBase[l] = base;
    }
    liveId[l]  = id;
    tweaked[l] = false;
}

// The layer's live voice, as a saved voice.
static VoiceSlot toSlot(const SavedConfig& cfg, uint8_t l) {
    sync(cfg, l);
    const Voice& v = live[l];
    VoiceSlot s{};
    s.used       = true;
    s.base       = liveBase[l];
    s.wave       = (uint8_t)voiceWave(v.waveform) | (v.harmonicsEdited ? SLOT_HARMONICS_EDITED : 0);
    s.sustainPct = (uint8_t)constrain((int)lroundf(v.sustain * 100.0f), 0, 100);
    s.attackMs   = v.attackMs;
    s.decayMs    = v.decayMs;
    s.releaseMs  = v.releaseMs;
    s.noteMs     = v.noteMs;
    memcpy(s.harmonics, v.harmonics, NUM_HARMONICS);
    s.filter     = v.filter;
    return s;
}

bool layerActive(const SavedConfig& cfg, uint8_t layer) {
    LayerMode m = cfg.layer[layer].mode;
    if (layer == LAYER_B && m == LayerMode::SameAsA) m = cfg.layer[LAYER_A].mode;
    return m == LayerMode::On || m == LayerMode::Stack;
}

const LayerCfg& layerEffective(const SavedConfig& cfg, uint8_t layer) {
    if (layer == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
        return cfg.layer[LAYER_A];
    }
    return cfg.layer[layer];
}

const Voice& layerVoice(const SavedConfig& cfg, uint8_t layer) {
    uint8_t src = voiceSource(cfg, layer);
    sync(cfg, src);
    return live[src];
}

Voice& layerVoiceEdit(const SavedConfig& cfg, uint8_t layer) {
    uint8_t src = voiceSource(cfg, layer);
    sync(cfg, src);
    return live[src];
}

void layerVoiceTweaked(const SavedConfig& cfg, uint8_t layer) {
    tweaked[voiceSource(cfg, layer)] = true;
    layersApply(cfg);
}

bool layerVoiceIsTweaked(const SavedConfig& cfg, uint8_t layer) {
    uint8_t src = voiceSource(cfg, layer);
    sync(cfg, src);
    return tweaked[src];
}

void layersResetVoices() {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) liveId[l] = NO_VOICE;
}

void layerVoiceSaveCustom(SavedConfig& cfg, uint8_t layer, uint8_t n) {
    if (n >= NUM_CUSTOM_VOICES) return;
    uint8_t src = voiceSource(cfg, layer);
    cfg.customVoices[n]   = toSlot(cfg, src);
    cfg.layer[src].voice  = VOICE_CUSTOM_FIRST + n;
    storageSave(cfg);
    layersResetVoices();   // every layer playing Custom n picks up the new one
    layersApply(cfg);
}

void layerVoiceSaveScene(SavedConfig& cfg, uint8_t layer) {
    uint8_t sc = cfg.currentScene;
    if (sc == SCENE_DEFAULTS || sc >= NUM_SCENES) return;
    uint8_t src = voiceSource(cfg, layer);
    cfg.sceneVoices[sc][src]          = toSlot(cfg, src);
    cfg.scenes[sc].layer[src].voice   = VOICE_SCENE;
    cfg.layer[src].voice              = VOICE_SCENE;
    storageSave(cfg);
    layersResetVoices();
    layersApply(cfg);
}

uint8_t layerChannel(const SavedConfig& cfg, uint8_t layer) {
    uint8_t ch = layerEffective(cfg, layer).channel;
    if (ch == LAYER_CHANNEL_AUTO || ch > 16) ch = layerVoice(cfg, layer).autoChannel;
    return ch;
}

uint8_t layerShiftSource(const SavedConfig& cfg, uint8_t layer) {
    if (layer == LAYER_B && (cfg.layer[LAYER_B].mode == LayerMode::SameAsA ||
                             cfg.layer[LAYER_B].shiftSameAsA)) {
        return LAYER_A;
    }
    return layer;
}

uint8_t layerLowNoteSource(const SavedConfig& cfg, uint8_t layer) {
    if (layer == LAYER_B && (cfg.layer[LAYER_B].mode == LayerMode::SameAsA ||
                             cfg.layer[LAYER_B].lowNoteSameAsA)) {
        return LAYER_A;
    }
    return layer;
}

bool layerWraps(const SavedConfig& cfg, uint8_t layer) {
    if (voiceIsKit(layerVoice(cfg, layer))) return true;
    return cfg.layer[layerShiftSource(cfg, layer)].wrap;
}

uint8_t layerDegree(const SavedConfig& cfg, uint8_t layer, uint8_t sensor) {
    const LayerCfg& src = cfg.layer[layerShiftSource(cfg, layer)];
    bool outer = cfg.layer[layerLowNoteSource(cfg, layer)].lowNote == (uint8_t)LowNote::Outer;
    // Flip first, then shift. Shifting before the flip would make the shift
    // run backwards on an Outer layer.
    uint8_t base   = outer ? (uint8_t)(NUM_HALL_SENSORS - 1 - sensor) : sensor;
    uint8_t degree = base + (src.shift % NUM_HALL_SENSORS);
    if (layerWraps(cfg, layer)) degree %= NUM_HALL_SENSORS;
    return degree;
}

uint8_t layerFxSource(const SavedConfig& cfg, uint8_t layer, FxId fx) {
    if (layer == LAYER_B && (cfg.layer[LAYER_B].mode == LayerMode::SameAsA ||
                             (cfg.layer[LAYER_B].fx.sameAsA & (1u << (uint8_t)fx)))) {
        return LAYER_A;
    }
    return layer;
}

LayerFx layerFx(const SavedConfig& cfg, uint8_t layer) {
    LayerFx out = cfg.layer[layer].fx;
    const LayerFx& tone   = cfg.layer[layerFxSource(cfg, layer, FxId::Tone)].fx;
    const LayerFx& chorus = cfg.layer[layerFxSource(cfg, layer, FxId::Chorus)].fx;
    const LayerFx& delay  = cfg.layer[layerFxSource(cfg, layer, FxId::Delay)].fx;
    const LayerFx& reverb = cfg.layer[layerFxSource(cfg, layer, FxId::Reverb)].fx;
    out.cutoff        = tone.cutoff;
    out.resonance     = tone.resonance;
    out.chorusRate    = chorus.chorusRate;
    out.chorusDepth   = chorus.chorusDepth;
    out.chorusMix     = chorus.chorusMix;
    out.delayMode     = delay.delayMode;
    out.delaySync     = delay.delaySync;
    out.delayMs       = delay.delayMs;
    out.delayFeedback = delay.delayFeedback;
    out.delayMix      = delay.delayMix;
    out.roomSize      = reverb.roomSize;
    out.damping       = reverb.damping;
    out.reverbMix     = reverb.reverbMix;
    return out;
}

float layerGain(const SavedConfig& cfg, uint8_t layer) {
    float level = constrain(layerEffective(cfg, layer).level, 0, 100) / 100.0f;

    // Each side stays at full level until the balance moves AWAY from it, so
    // centre is "both as set" rather than "both at half".
    int8_t toward = (layer == LAYER_A) ? -balance : balance;
    float  fade   = (toward >= 0) ? 1.0f
                  : (float)(BALANCE_STEPS + toward) / (float)BALANCE_STEPS;
    return level * fade;
}

int8_t layerBalance() {
    return balance;
}

void layerSetBalance(int8_t b) {
    balance = (int8_t)constrain(b, -BALANCE_STEPS, BALANCE_STEPS);
}

void layersApplyEffects(const SavedConfig& cfg) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) audioSetEffects(l, layerFx(cfg, l));
    layersUpdate(cfg);
}

// Order is the stored position: append only.
struct SyncTime { uint8_t num, den; const char* name; };
static const SyncTime SYNC_TIMES[DELAY_SYNC_COUNT] = {
    { 1, 1, "1" },    { 1, 2, "1/2" },   { 3, 8, "3/8" },   { 1, 3, "1/3" },
    { 1, 4, "1/4" },  { 1, 5, "1/5" },   { 1, 6, "1/6" },   { 3, 16, "3/16" },
    { 1, 8, "1/8" },  { 1, 10, "1/10" }, { 1, 12, "1/12" }, { 1, 16, "1/16" },
    { 1, 32, "1/32" },
};

const char* delaySyncName(uint8_t i) {
    return SYNC_TIMES[i < DELAY_SYNC_COUNT ? i : 0].name;
}

// 10-100 ms in 10s, to 1000 in 50s, to 2400 in 100s.
uint16_t delayFreeMs(uint8_t i) {
    if (i >= DELAY_FREE_COUNT) i = DELAY_FREE_COUNT - 1;
    if (i < 10) return (uint16_t)(10 * (i + 1));
    if (i < 28) return (uint16_t)(100 + 50 * (i - 9));
    return (uint16_t)(1000 + 100 * (i - 27));
}
static_assert(DELAY_FREE_COUNT == 42, "the Free list runs 10 ms to 2400 ms");

uint8_t delayFreeIndex(uint16_t ms) {
    uint8_t best = 0;
    for (uint8_t i = 1; i < DELAY_FREE_COUNT; i++) {
        if (abs((int)delayFreeMs(i) - (int)ms) < abs((int)delayFreeMs(best) - (int)ms)) best = i;
    }
    return best;
}

float layerDelayMs(const SavedConfig& cfg, uint8_t layer, float lastMs) {
    const LayerFx& fx = cfg.layer[layerFxSource(cfg, layer, FxId::Delay)].fx;
    if (fx.delayMode == (uint8_t)DelayMode::Free) {
        return (float)constrain((int)fx.delayMs, 10, (int)DELAY_MAX_MS);
    }
    float bpm = fabsf(cfg.rpm) * (float)cfg.beatsPerRev;
    if (bpm <= 0.0f) return lastMs;
    const SyncTime& t = SYNC_TIMES[fx.delaySync < DELAY_SYNC_COUNT ? fx.delaySync : 0];
    float ms = 60000.0f / bpm * t.num / t.den;
    while (ms > (float)DELAY_MAX_MS) ms *= 0.5f;
    return ms;
}

void layersUpdate(const SavedConfig& cfg) {
    static float last[NUM_LAYERS] = { 0.0f, 0.0f };
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        float ms = layerDelayMs(cfg, l, last[l]);
        if (ms != last[l]) {
            last[l] = ms;
            audioSetDelayTime(l, ms);
        }
    }
}

void layersApply(const SavedConfig& cfg) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        audioSetVoice(l, layerVoice(cfg, l));
        audioSetEffects(l, layerFx(cfg, l));
    }
    layersUpdate(cfg);
    // The shared kit goes through the effects of the layer playing it, A's
    // when both do.
    bool kitB = layerActive(cfg, LAYER_B) && voiceIsKit(layerVoice(cfg, LAYER_B));
    bool kitA = layerActive(cfg, LAYER_A) && voiceIsKit(layerVoice(cfg, LAYER_A));
    audioSetDrumLayer((kitB && !kitA) ? LAYER_B : LAYER_A);
    // Kit layers are left out: bend on a drum channel would retune the drums.
    // A silent layer sends nothing, so it needs no bend either.
    auto bendChannel = [&](uint8_t l) -> uint8_t {
        const Voice& v = layerVoice(cfg, l);
        if (!layerActive(cfg, l) || voiceIsKit(v) || voiceIsSilent(v)) return 0;
        return layerChannel(cfg, l);
    };
    midiSetLayerChannels(bendChannel(LAYER_A), bendChannel(LAYER_B));
}
