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
// Loading or saving a scene also makes its values the saved main settings,
// as if they had been picked in the menu, so power-up plays the last scene
// plus any menu changes made since. There is one "saved" state, not two.
// =============================================================================

void    scenesInit(const SavedConfig& cfg);   // at boot: the scene's pitch and balance
void    scenesUpdate(SavedConfig& cfg);       // call every loop: lands a queued load

bool    sceneUsed(const SavedConfig& cfg, uint8_t slot);
bool    sceneAnyUsed(const SavedConfig& cfg);

// The scene shown as selected: the one queued to load, else the current one.
uint8_t sceneSelected(const SavedConfig& cfg);

// Queues `slot` to load at the next bar start (or now, see above).
void    sceneQueue(SavedConfig& cfg, uint8_t slot);

// Saves the live settings to `slot` and makes it the current scene.
void    sceneSave(SavedConfig& cfg, uint8_t slot);

// True when the live settings differ from the current scene.
bool    sceneModified(const SavedConfig& cfg);

// Aux Reset All: back to the saved settings, with the current scene's pitch
// and balance (centred if none).
void    sceneRevertLive(SavedConfig& cfg);

// True once after a load has landed, so the live display can redraw.
bool    sceneTakeChanged();
