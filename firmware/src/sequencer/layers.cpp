#include "layers.h"
#include <Arduino.h>
#include "audio/audio.h"
#include "midi/midi.h"

static int8_t balance = 0;

// Each layer's live copy of its voice, and which voice it is a copy of.
// A layer set to a different voice starts again from stock.
static const uint8_t NO_VOICE = 0xFF;
static Voice   live[NUM_LAYERS];
static uint8_t liveId[NUM_LAYERS]  = { NO_VOICE, NO_VOICE };
static bool    tweaked[NUM_LAYERS] = { false, false };

// Whose voice this layer plays: Layer A's for B in Same as A.
static uint8_t voiceSource(const SavedConfig& cfg, uint8_t layer) {
    return (layer == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) ? LAYER_A : layer;
}

static void sync(const SavedConfig& cfg, uint8_t l) {
    uint8_t id = cfg.layer[l].voice;
    if (liveId[l] == id) return;
    live[l]    = voiceGet(id);
    liveId[l]  = id;
    tweaked[l] = false;
}

bool layerActive(const SavedConfig& cfg, uint8_t layer) {
    LayerMode m = cfg.layer[layer].mode;
    if (layer == LAYER_B && m == LayerMode::SameAsA) m = cfg.layer[LAYER_A].mode;
    return m == LayerMode::On;
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

void layersApply(const SavedConfig& cfg) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        audioSetVoice(l, layerVoice(cfg, l));
    }
    // Kit layers are left out: bend on a drum channel would retune the drums.
    // A silent layer sends nothing, so it needs no bend either.
    auto bendChannel = [&](uint8_t l) -> uint8_t {
        const Voice& v = layerVoice(cfg, l);
        if (!layerActive(cfg, l) || voiceIsKit(v) || voiceIsSilent(v)) return 0;
        return layerChannel(cfg, l);
    };
    midiSetLayerChannels(bendChannel(LAYER_A), bendChannel(LAYER_B));
}
