#pragma once
#include <stdint.h>
#include "config/storage.h"
#include "audio/voice.h"

// Resolves the two magnet layers into what actually plays. The saved settings
// live in cfg.layer[]; this is where "Same as A" and "Auto" channel are turned
// into concrete values, so nothing else has to know about either.

// Is this layer making sound? Layer B in Same as A follows Layer A's On/Off.
bool layerActive(const SavedConfig& cfg, uint8_t layer);

// The settings this layer plays with. Layer B in Same as A returns Layer A's.
const LayerCfg& layerEffective(const SavedConfig& cfg, uint8_t layer);

const Voice& layerVoice(const SavedConfig& cfg, uint8_t layer);

// MIDI channel 1-16, with Auto resolved from the voice.
uint8_t layerChannel(const SavedConfig& cfg, uint8_t layer);

// Output gain 0.0-1.0: the layer's level times the A/B balance. Applied as
// note velocity, which scales the internal synth and external synths alike.
float layerGain(const SavedConfig& cfg, uint8_t layer);

// --- A/B balance --------------------------------------------------------------
// A live crossfade between the two layers, driven by the aux knob. RAM only,
// like the pitch offset: a session always starts centred. -BALANCE_STEPS is
// all A, +BALANCE_STEPS all B, 0 both at their own level.
constexpr int8_t BALANCE_STEPS = 10;
int8_t layerBalance();
void   layerSetBalance(int8_t balance);

// Pushes voices and MIDI channels to the audio and MIDI layers. Call after
// anything that changes a layer setting. The balance needs no apply: it is
// read per note, in layerGain().
void layersApply(const SavedConfig& cfg);
