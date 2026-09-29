#pragma once
#include <stdint.h>
#include "config/storage.h"
#include "audio/voice.h"

// Resolves the two magnet layers into what actually plays. The live sound
// is in cfg.layer[]; this is where "Same as A" and "Auto" channel are turned
// into concrete values, so nothing else has to know about either.

// Is this layer making sound? Layer B in Same as A follows Layer A's On/Off.
bool layerActive(const SavedConfig& cfg, uint8_t layer);

// The settings this layer plays with. Layer B in Same as A returns Layer A's.
const LayerCfg& layerEffective(const SavedConfig& cfg, uint8_t layer);

// The voice this layer plays: its live copy (see Voice Edit below). Layer B
// in Same as A plays Layer A's, tweaks included.
const Voice& layerVoice(const SavedConfig& cfg, uint8_t layer);

// MIDI channel 1-16, with Auto resolved from the voice.
uint8_t layerChannel(const SavedConfig& cfg, uint8_t layer);

// --- Track Shift --------------------------------------------------------------
// Which layer's shift and Wrap this layer plays with: A's for Layer B in Same
// as A, or with B's shift bound to A; otherwise its own.
uint8_t layerShiftSource(const SavedConfig& cfg, uint8_t layer);

// Which layer's Low Note this layer plays with. Its own binding, separate
// from the shift's, so B can follow A's shift while flipped.
uint8_t layerLowNoteSource(const SavedConfig& cfg, uint8_t layer);

// Does this layer's shifted degree wrap around the arm? Kit voices always do:
// the kit has exactly one drum per sensor.
bool layerWraps(const SavedConfig& cfg, uint8_t layer);

// The scale degree (or kit slot) a sensor plays on this layer: Low Note flips
// the sensor order first, then the shift is added, so shifting up raises the
// pitch whichever end is low. With Wrap it is 0 to NUM_HALL_SENSORS-1;
// without, it runs on past the end and scaleNote() carries it into the next
// octave.
uint8_t layerDegree(const SavedConfig& cfg, uint8_t layer, uint8_t sensor);

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

// --- Voice Edit -----------------------------------------------------------------
// Each layer plays a live copy of its voice, which Voice Edit can tweak.
// Tweaks are live only: choosing another voice, loading a scene or Aux Reset
// All goes back to the stock voice.

// The live copy for Voice Edit to change. Layer B in Same as A gets Layer A's.
// Call layerVoiceTweaked() after changing it.
Voice& layerVoiceEdit(const SavedConfig& cfg, uint8_t layer);

// Marks the layer's voice as tweaked and pushes it to the synth.
void   layerVoiceTweaked(const SavedConfig& cfg, uint8_t layer);

bool   layerVoiceIsTweaked(const SavedConfig& cfg, uint8_t layer);

// Drops every tweak: each layer goes back to its stock voice. Call before
// layersApply().
void   layersResetVoices();

// Pushes voices and MIDI channels to the audio and MIDI layers. Call after
// anything that changes a layer setting. The balance needs no apply: it is
// read per note, in layerGain().
void layersApply(const SavedConfig& cfg);
