#include "layers.h"
#include <Arduino.h>
#include "audio/audio.h"
#include "midi/midi.h"

static int8_t balance = 0;

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
    return voiceGet(layerEffective(cfg, layer).voice);
}

uint8_t layerChannel(const SavedConfig& cfg, uint8_t layer) {
    uint8_t ch = layerEffective(cfg, layer).channel;
    if (ch == LAYER_CHANNEL_AUTO || ch > 16) ch = layerVoice(cfg, layer).autoChannel;
    return ch;
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
    auto bendChannel = [&](uint8_t l) -> uint8_t {
        if (!layerActive(cfg, l) || voiceIsKit(layerVoice(cfg, l))) return 0;
        return layerChannel(cfg, l);
    };
    midiSetLayerChannels(bendChannel(LAYER_A), bendChannel(LAYER_B));
}
