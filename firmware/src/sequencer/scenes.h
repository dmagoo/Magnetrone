#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// Scenes -- the saved sounds of the table.
//
// A scene holds the whole sound: both layers (mode, voice, key, octave, level,
// shift and the rest) plus the Pitch offset and A/B Balance. The table plays
// the current scene, with any Aux tweaks on top of it.
//
//   Scene 0, Defaults   what the Sound Defaults menu edits. Always there.
//   Scenes 1-8          change only through Save Scene.
//
// Aux tweaks are live only: power-up loads the current scene afresh, and Aux
// Reset All goes back to it. Save Scene keeps them, in a slot.
//
// Loading a scene from the Aux lands on the next bar start, so it can be
// picked any time and falls on the downbeat; with the platter stopped or the
// start unknown it applies at once.
// =============================================================================

void    scenesInit(SavedConfig& cfg);     // at boot: loads the current scene
void    scenesUpdate(SavedConfig& cfg);   // call every loop: lands a queued load

bool    sceneUsed(const SavedConfig& cfg, uint8_t slot);

// The scene shown as selected: the one queued to load, else the current one.
uint8_t sceneSelected(const SavedConfig& cfg);

// Queues `slot` to load at the next bar start (or now, see above).
void    sceneQueue(SavedConfig& cfg, uint8_t slot);

// Loads `slot` at once, dropping any queued load.
void    sceneLoadNow(SavedConfig& cfg, uint8_t slot);

// Saves the live sound to `slot` (1-8) and makes it the current scene.
void    sceneSave(SavedConfig& cfg, uint8_t slot);

// True when the live sound differs from the current scene.
bool    sceneModified(const SavedConfig& cfg);

// Aux Reset All: back to the current scene as saved.
void    sceneRevertLive(SavedConfig& cfg);

// True once after a load has landed, so the live display can redraw.
bool    sceneTakeChanged();
