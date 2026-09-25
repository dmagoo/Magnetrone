#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// Scenes -- saved Aux setups, recalled live.
//
// A scene holds everything the Aux knob can change (root, scale, octave, each
// layer's voice, shift and Low Note, pitch, A/B balance). Loading one lands on
// the next bar start, so it can be picked any time and falls on the downbeat;
// with the platter stopped or the start unknown it applies at once.
//
// Besides the 8 slots there is a read-only Defaults scene (SCENE_DEFAULTS):
// the factory values of the same fields. It can be loaded, never saved over.
//
// Loading or saving a scene also makes its values the saved main settings,
// as if they had been picked in the menu, so power-up plays the last scene
// plus any menu changes made since. There is one "saved" state, not two.
// =============================================================================

void    scenesInit(SavedConfig& cfg);   // at boot: the scene's pitch, balance, learned scale
void    scenesUpdate(SavedConfig& cfg);       // call every loop: lands a queued load

// True for a saved slot, and always for SCENE_DEFAULTS.
bool    sceneUsed(const SavedConfig& cfg, uint8_t slot);

// The scene shown as selected: the one queued to load, else the current one.
uint8_t sceneSelected(const SavedConfig& cfg);

// Queues `slot` to load at the next bar start (or now, see above).
void    sceneQueue(SavedConfig& cfg, uint8_t slot);

// Saves the live settings to `slot` and makes it the current scene.
void    sceneSave(SavedConfig& cfg, uint8_t slot);

// True when the live settings differ from the current scene.
bool    sceneModified(const SavedConfig& cfg);

// Aux Reset All: back to the saved settings, with the current scene's pitch,
// balance and learned scale (centred / none if no scene).
void    sceneRevertLive(SavedConfig& cfg);

// True once after a load has landed, so the live display can redraw.
bool    sceneTakeChanged();
